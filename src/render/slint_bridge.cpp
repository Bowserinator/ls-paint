#include "render/slint_bridge.hpp"

#include <cstring>

namespace lsp {

static_assert(sizeof(Rgba8) == sizeof(slint::Rgba8Pixel));

void SlintBridge::upload(const PixelBuffer& source, RectI region) {
    const auto width = static_cast<std::uint32_t>(source.width());
    const auto height = static_cast<std::uint32_t>(source.height());
    if (m_pixels.width() != width || m_pixels.height() != height) {
        m_pixels = slint::SharedPixelBuffer<slint::Rgba8Pixel>(width, height);
        region = source.bounds();
    }

    region = region.intersected(source.bounds());
    slint::Rgba8Pixel* destination = m_pixels.begin();
    const auto stride = static_cast<std::size_t>(m_pixels.width());

    for (int y = region.y; y < region.bottom(); ++y) {
        const auto row = source.row(y).subspan(static_cast<std::size_t>(region.x), static_cast<std::size_t>(region.width));
        std::memcpy(destination + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(region.x),
                    row.data(), row.size_bytes());
    }
}

slint::Image image_from(const PixelBuffer& source) {
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(static_cast<std::uint32_t>(source.width()),
                                                     static_cast<std::uint32_t>(source.height()));

    for (int y = 0; y < source.height(); ++y) {
        const auto row = source.row(y);
        std::memcpy(pixels.begin() + static_cast<std::size_t>(y) * pixels.width(), row.data(),
                    row.size_bytes());
    }
    return slint::Image(pixels);
}

}  // namespace lsp
