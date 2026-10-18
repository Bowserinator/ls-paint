#include "selection/selection_mask.hpp"

#include <algorithm>
#include <limits>

#include "utils/math.hpp"
#include "utils/scanline.hpp"

namespace lsp {

void SelectionMask::ensure_allocated() {
    if (m_mask.empty()) m_mask.assign(static_cast<std::size_t>(m_width) * m_height, 0);
}

std::uint8_t& SelectionMask::cell(int x, int y) { return m_mask[grid_index(x, y, m_width)]; }

std::uint8_t SelectionMask::cell(int x, int y) const { return m_mask[grid_index(x, y, m_width)]; }

void SelectionMask::recompute_bounds() {
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = -1;
    int max_y = -1;

    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            if (cell(x, y) == 0) continue;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }

    m_bounds = max_x >= min_x ? RectI{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1} : RectI{};
    if (m_bounds.empty()) clear();
}

void SelectionMask::fill_rect(RectI rect) {
    rect = rect.intersected({0, 0, m_width, m_height});
    if (rect.empty()) {
        clear();
        return;
    }
    ensure_allocated();
    std::ranges::fill(m_mask, 0);
    for (int y = rect.y; y < rect.bottom(); ++y)
        std::fill_n(m_mask.begin() + static_cast<std::ptrdiff_t>(y) * m_width + rect.x, rect.width, 1);
    m_bounds = rect;
}

void SelectionMask::fill_polygon(std::span<const Point2f> points) {
    if (points.size() < 3) {
        clear();
        return;
    }

    ensure_allocated();
    std::ranges::fill(m_mask, 0);

    std::vector<float> crossings;
    for (int y = 0; y < m_height; ++y) {
        polygon_crossings(points, static_cast<float>(y) + 0.5f, crossings);
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int left = std::clamp(static_cast<int>(crossings[i]), 0, m_width);
            const int right = std::clamp(static_cast<int>(crossings[i + 1]), 0, m_width);
            for (int x = left; x < right; ++x) cell(x, y) = 1;
        }
    }
    recompute_bounds();
}

void SelectionMask::invert() {
    ensure_allocated();
    for (std::uint8_t& value : m_mask) value = value ? 0 : 1;
    recompute_bounds();
}

void SelectionMask::replace_region(RectI region, const std::vector<std::uint8_t>& bytes) {
    if (region.empty() || bytes.size() != static_cast<std::size_t>(region.width) * region.height) return;
    const RectI clipped = region.intersected({0, 0, m_width, m_height});
    if (clipped.empty()) return;
    ensure_allocated();
    for (int y = clipped.y; y < clipped.bottom(); ++y) {
        const auto source = bytes.begin() + static_cast<std::ptrdiff_t>(y - region.y) * region.width +
                            (clipped.x - region.x);
        std::copy_n(source, clipped.width,
                    m_mask.begin() + static_cast<std::ptrdiff_t>(y) * m_width + clipped.x);
    }
    recompute_bounds();
}

std::vector<std::uint8_t> SelectionMask::extract_region(RectI region) const {
    if (region.empty()) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(region.width) * region.height, 0);
    for (int y = region.y; y < region.bottom(); ++y) {
        if (y < 0 || y >= m_height) continue;
        for (int x = region.x; x < region.right(); ++x) {
            if (x < 0 || x >= m_width || m_mask.empty()) continue;
            bytes[grid_index(x - region.x, y - region.y, region.width)] = cell(x, y);
        }
    }
    return bytes;
}

}  // namespace lsp
