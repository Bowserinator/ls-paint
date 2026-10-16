#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/pixel_buffer.hpp"

namespace lsp {

// The system clipboard. Straight (non-premultiplied) RGBA8 for images.
class Clipboard {
public:
    // Puts `image` on the clipboard. False when it cannot be locked.
    [[nodiscard]] static bool set_image(const PixelBuffer& image);
    // Reads an image off the clipboard. Nullopt when there is none.
    [[nodiscard]] static std::optional<PixelBuffer> get_image();
    // Reads the clipboard as text. False when it holds no text.
    [[nodiscard]] static bool get_text(std::string& text);
    // True when the clipboard advertises an image, whether or not it can be read.
    [[nodiscard]] static bool has_image();
    // The clipboard's advertised types, for diagnosing a paste that fails.
    [[nodiscard]] static std::vector<std::string> available_types();
};

}  // namespace lsp
