#include "selection/selection_mask.hpp"

#include <algorithm>
#include <limits>

namespace lsp {

void SelectionMask::ensure_allocated() {
    if (m_mask.empty()) m_mask.assign(static_cast<std::size_t>(m_width) * m_height, 0);
}

void SelectionMask::recompute_bounds() {
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = -1;
    int max_y = -1;

    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            if (m_mask[static_cast<std::size_t>(y) * m_width + x] == 0) continue;
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

void SelectionMask::fill_polygon(const std::vector<Point2f>& points) {
    if (points.size() < 3) {
        clear();
        return;
    }

    ensure_allocated();
    std::ranges::fill(m_mask, 0);

    for (int y = 0; y < m_height; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        std::vector<float> crossings;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Point2f& a = points[i];
            const Point2f& b = points[(i + 1) % points.size()];
            if ((a.y <= py && b.y > py) || (b.y <= py && a.y > py))
                crossings.push_back(a.x + (py - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::ranges::sort(crossings);
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int left = std::clamp(static_cast<int>(crossings[i]), 0, m_width);
            const int right = std::clamp(static_cast<int>(crossings[i + 1]), 0, m_width);
            for (int x = left; x < right; ++x) m_mask[static_cast<std::size_t>(y) * m_width + x] = 1;
        }
    }
    recompute_bounds();
}

void SelectionMask::invert() {
    ensure_allocated();
    for (std::uint8_t& value : m_mask) value = value ? 0 : 1;
    recompute_bounds();
}

void SelectionMask::translate(int dx, int dy) {
    if (m_mask.empty() || (dx == 0 && dy == 0)) return;
    std::vector<std::uint8_t> moved(m_mask.size(), 0);
    for (int y = 0; y < m_height; ++y) {
        const int target_y = y + dy;
        if (target_y < 0 || target_y >= m_height) continue;
        for (int x = 0; x < m_width; ++x) {
            if (m_mask[static_cast<std::size_t>(y) * m_width + x] == 0) continue;
            const int target_x = x + dx;
            if (target_x < 0 || target_x >= m_width) continue;
            moved[static_cast<std::size_t>(target_y) * m_width + target_x] = 1;
        }
    }
    m_mask = std::move(moved);
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
            bytes[static_cast<std::size_t>(y - region.y) * region.width + (x - region.x)] =
                m_mask[static_cast<std::size_t>(y) * m_width + x];
        }
    }
    return bytes;
}

}  // namespace lsp
