#pragma once

#include <optional>
#include <string>

#include "core/pixel_buffer.hpp"

namespace lsp {

// The system clipboard. Straight (non-premultiplied) RGBA8 for images.
class Clipboard {
public:
    // Puts `region` of `image` on the clipboard. False when it cannot be locked.
    [[nodiscard]] static bool set_image(const PixelBuffer& image, RectI region);
    // Reads an image off the clipboard. Nullopt when there is none, or when the
    // image is over the size limit, which `error` then explains.
    [[nodiscard]] static std::optional<PixelBuffer> get_image(std::string* error = nullptr);
    // True when the clipboard advertises an image, whether or not it can be read.
    [[nodiscard]] static bool has_image();
};

}  // namespace lsp
