#include "core/pixel_buffer.hpp"

#include <algorithm>

namespace lsp {

PixelBuffer PixelBuffer::copy_region(RectI region) const {
    region = region.intersected(bounds());
    PixelBuffer result(region.width, region.height);
    for (int y = 0; y < region.height; ++y) {
        const auto source = row(region.y + y).subspan(static_cast<std::size_t>(region.x), static_cast<std::size_t>(region.width));
        std::ranges::copy(source, result.row(y).begin());
    }
    return result;
}

void PixelBuffer::blit(int x, int y, const PixelBuffer& source) {
    const RectI area = RectI{x, y, source.width(), source.height()}.intersected(bounds());
    for (int target_y = area.y; target_y < area.bottom(); ++target_y) {
        const auto pixels = source.row(target_y - y).subspan(static_cast<std::size_t>(area.x - x), static_cast<std::size_t>(area.width));
        std::ranges::copy(pixels, row(target_y).begin() + area.x);
    }
}

PixelBuffer PixelBuffer::rotated_90_cw() const {
    PixelBuffer result(m_height, m_width);
    for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
            result.at(m_height - 1 - y, x) = at(x, y);
    return result;
}

PixelBuffer PixelBuffer::flipped_horizontal() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x)
            result.at(m_width - 1 - x, y) = at(x, y);
    return result;
}

PixelBuffer PixelBuffer::flipped_vertical() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        std::ranges::copy(row(y), result.row(m_height - 1 - y).begin());
    return result;
}

}  // namespace lsp
