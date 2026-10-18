#include "core/pixel_buffer.hpp"

#include <algorithm>
#include <unordered_set>

#include "config.hpp"

namespace lsp {

PixelBuffer::PixelBuffer(int width, int height, Rgba8 fill) : m_width(width), m_height(height) {
    if (width <= 0 || height <= 0) {
        m_width = m_height = 0;
        return;
    }

    const auto rows_per_band = std::max<std::size_t>(1, kPixelBandPixels / static_cast<std::size_t>(width));
    const auto band_rows = std::bit_floor(rows_per_band);
    m_band_shift = static_cast<unsigned>(std::countr_zero(band_rows));
    m_band_mask = band_rows - 1;

    const auto row_count = static_cast<std::size_t>(height);
    const std::size_t band_count = (row_count + band_rows - 1) / band_rows;
    const auto make_band = [&](std::size_t rows) {
        return std::make_shared<std::vector<Rgba8>>(rows * static_cast<std::size_t>(width), fill);
    };

    // Every full band starts out as the same storage and is split off when
    // written to.
    m_bands.reserve(band_count);
    const Band full = make_band(std::min(band_rows, row_count));
    for (std::size_t i = 0; i < band_count; ++i) {
        const std::size_t rows = std::min(band_rows, row_count - i * band_rows);
        m_bands.push_back(rows == band_rows ? full : make_band(rows));
    }
}

bool operator==(const PixelBuffer& a, const PixelBuffer& b) {
    if (a.m_width != b.m_width || a.m_height != b.m_height) return false;
    for (int y = 0; y < a.m_height; ++y) {
        if (a.row_shared_with(b, y)) continue;
        if (!std::ranges::equal(a.row(y), b.row(y))) return false;
    }
    return true;
}

bool PixelBuffer::row_shared_with(const PixelBuffer& other, int y) const noexcept {
    if (m_width != other.m_width || m_band_shift != other.m_band_shift) return false;
    const auto band = static_cast<std::size_t>(y) >> m_band_shift;
    return m_bands[band] == other.m_bands[band];
}

std::size_t PixelBuffer::storage_bytes() const noexcept {
    std::unordered_set<const void*> seen;
    std::size_t total = 0;
    for (const Band& band : m_bands)
        if (seen.insert(band.get()).second) total += band->size() * sizeof(Rgba8);
    return total;
}

std::vector<Rgba8> PixelBuffer::contiguous_copy(RectI region) const {
    region = region.intersected(bounds());
    std::vector<Rgba8> result;
    result.reserve(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height));
    for (int y = region.y; y < region.bottom(); ++y) {
        const auto source = row(y).subspan(static_cast<std::size_t>(region.x),
                                           static_cast<std::size_t>(region.width));
        result.insert(result.end(), source.begin(), source.end());
    }
    return result;
}

PixelBuffer PixelBuffer::copy_region(RectI region) const {
    region = region.intersected(bounds());
    PixelBuffer result(region.width, region.height);
    for (int y = 0; y < region.height; ++y) {
        const auto source = row(region.y + y).subspan(static_cast<std::size_t>(region.x),
                                                      static_cast<std::size_t>(region.width));
        std::ranges::copy(source, result.row(y).begin());
    }
    return result;
}

void PixelBuffer::blit(int x, int y, const PixelBuffer& source) {
    const RectI area = RectI{x, y, source.width(), source.height()}.intersected(bounds());
    for (int target_y = area.y; target_y < area.bottom(); ++target_y) {
        const auto pixels = source.row(target_y - y).subspan(static_cast<std::size_t>(area.x - x),
                                                             static_cast<std::size_t>(area.width));
        std::ranges::copy(pixels, row(target_y).begin() + area.x);
    }
}

void PixelBuffer::copy_region_from(const PixelBuffer& source, RectI region) {
    if (source.m_width != m_width || source.m_height != m_height) return;
    region = region.intersected(bounds());
    for (int y = region.y; y < region.bottom(); ++y) {
        if (row_shared_with(source, y)) continue;
        const auto pixels = source.row(y).subspan(static_cast<std::size_t>(region.x),
                                                  static_cast<std::size_t>(region.width));
        std::ranges::copy(pixels, row(y).begin() + region.x);
    }
}

PixelBuffer PixelBuffer::rotated_90_cw() const {
    PixelBuffer result(m_height, m_width);
    for (int y = 0; y < m_height; ++y) {
        const auto source = row(y);
        for (int x = 0; x < m_width; ++x) result.at(m_height - 1 - y, x) = source[static_cast<std::size_t>(x)];
    }
    return result;
}

PixelBuffer PixelBuffer::rotated_90_ccw() const {
    PixelBuffer result(m_height, m_width);
    for (int y = 0; y < m_height; ++y) {
        const auto source = row(y);
        for (int x = 0; x < m_width; ++x) result.at(y, m_width - 1 - x) = source[static_cast<std::size_t>(x)];
    }
    return result;
}

PixelBuffer PixelBuffer::rotated_180() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        std::ranges::reverse_copy(row(y), result.row(m_height - 1 - y).begin());
    return result;
}

PixelBuffer PixelBuffer::flipped_horizontal() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y) std::ranges::reverse_copy(row(y), result.row(y).begin());
    return result;
}

PixelBuffer PixelBuffer::flipped_vertical() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        std::ranges::copy(row(y), result.row(m_height - 1 - y).begin());
    return result;
}

}  // namespace lsp
