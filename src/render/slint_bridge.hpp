#pragma once

#include <cstdint>

#include <slint.h>

#include "core/pixel_buffer.hpp"

namespace lsp {

class SlintBridge {
public:
    SlintBridge(int width, int height)
        : m_pixels(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)) {}

    void upload(const PixelBuffer& source, RectI region);

    [[nodiscard]] slint::Image image() const { return slint::Image(m_pixels); }

private:
    slint::SharedPixelBuffer<slint::Rgba8Pixel> m_pixels;
};

// Copies `source` into an image that stands on its own.
[[nodiscard]] slint::Image image_from(const PixelBuffer& source);

}  // namespace lsp
