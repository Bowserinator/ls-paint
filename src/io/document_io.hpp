#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/pixel_buffer.hpp"

namespace lsp {

class CanvasDocument;

enum class ImageFormat { Png, Bmp, Jpeg };

struct LoadResult {
    bool ok = false;
    PixelBuffer pixels;   // valid iff ok
    std::string error;    // non-empty iff !ok
};

// Decodes an image file to straight RGBA8.
[[nodiscard]] LoadResult load(const std::filesystem::path& path);

// Writes the flattened document. Alpha is flattened onto white for Bmp and Jpeg.
[[nodiscard]] bool save(const CanvasDocument& document, const std::filesystem::path& path,
                        ImageFormat format, std::string* error);

[[nodiscard]] std::optional<ImageFormat> format_from_extension(const std::filesystem::path& path);
[[nodiscard]] const char* extension_for(ImageFormat format);
[[nodiscard]] ImageFormat format_from_index(int index);
[[nodiscard]] int index_from(ImageFormat format);

// Composites translucent pixels over an opaque background.
[[nodiscard]] PixelBuffer flatten_to_opaque(const PixelBuffer& source,
                                            Rgba8 background = {255, 255, 255, 255});

}  // namespace lsp
