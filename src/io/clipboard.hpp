#pragma once

#include <optional>

#include "core/pixel_buffer.hpp"

namespace lsp {

// The system clipboard, image only. Straight (non-premultiplied) RGBA8.
class Clipboard {
public:
    // Puts `image` on the clipboard. False when it cannot be locked.
    [[nodiscard]] static bool set_image(const PixelBuffer& image);
    // Reads an image off the clipboard. Nullopt when there is none.
    [[nodiscard]] static std::optional<PixelBuffer> get_image();
};

}  // namespace lsp
