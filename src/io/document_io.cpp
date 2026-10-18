#include "io/document_io.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

#include "config.hpp"
#include "core/canvas_document.hpp"
#include "utils/math.hpp"
#include "utils/path_text.hpp"
#include "utils/text.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

namespace lsp {
namespace {

constexpr int kJpegQuality = 90;

// stb reads and writes through fopen, which cannot open a path outside the
// ANSI code page on Windows, so the file is opened here and handed over as a
// stream.
enum class FileMode { Read, Write };

std::FILE* open_file(const std::filesystem::path& path, FileMode mode) {
#ifdef _WIN32
    return _wfopen(path.wstring().c_str(), mode == FileMode::Read ? L"rb" : L"wb");
#else
    return std::fopen(utf8_from_path(path).c_str(), mode == FileMode::Read ? "rb" : "wb");
#endif
}

// The vendored stb_image_write wants a void callback, so a write error is
// caught by the fclose check in save() instead of here.
void write_stream(void* context, void* data, int size) {
    std::fwrite(data, 1, static_cast<std::size_t>(size), static_cast<std::FILE*>(context));
}

// stb_image_write takes three bytes per pixel for Bmp and Jpeg.
std::vector<std::uint8_t> to_rgb_bytes(const PixelBuffer& source) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(source.width()) * source.height() * 3);
    for (int y = 0; y < source.height(); ++y) {
        const Rgba8* pixels = source.row(y).data();
        std::uint8_t* row = bytes.data() + static_cast<std::size_t>(y) * source.width() * 3;
        for (int x = 0; x < source.width(); ++x) {
            row[static_cast<std::size_t>(x) * 3] = pixels[x].r;
            row[static_cast<std::size_t>(x) * 3 + 1] = pixels[x].g;
            row[static_cast<std::size_t>(x) * 3 + 2] = pixels[x].b;
        }
    }
    return bytes;
}

// Takes the pixels stb decoded and turns them into a LoadResult.
LoadResult from_decoded(std::uint8_t* data, int width, int height, const std::string& description) {
    if (width <= 0 || height <= 0 || width > kMaxImageDimension || height > kMaxImageDimension) {
        stbi_image_free(data);
        return {false, {}, "Image too large: " + description};
    }

    PixelBuffer pixels(width, height);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * sizeof(Rgba8);
    for (int y = 0; y < height; ++y) {
        std::memcpy(pixels.row(y).data(), data + static_cast<std::size_t>(y) * row_bytes, row_bytes);
    }
    stbi_image_free(data);
    return {true, std::move(pixels), {}};
}

}  // namespace

LoadResult load(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return {false, {}, "Not a file: " + utf8_from_path(path)};

    std::FILE* file = open_file(path, FileMode::Read);
    if (file == nullptr) return {false, {}, "Cannot open: " + utf8_from_path(path)};
    int width = 0;
    int height = 0;
    int channels = 0;
    std::uint8_t* data = stbi_load_from_file(file, &width, &height, &channels, 4);
    std::fclose(file);
    if (data == nullptr) return {false, {}, std::string("Cannot decode image: ") + stbi_failure_reason()};
    return from_decoded(data, width, height, utf8_from_path(path));
}

LoadResult load_from_memory(const void* data, std::size_t size) {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::uint8_t* pixels = stbi_load_from_memory(static_cast<const std::uint8_t*>(data),
                                                static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) return {false, {}, std::string("Cannot decode image: ") + stbi_failure_reason()};
    return from_decoded(pixels, width, height, "the pasted image");
}

bool save(const CanvasDocument& document, const std::filesystem::path& path, ImageFormat format,
          std::string* error) {
    const auto fail = [error](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error_code;
        if (!std::filesystem::is_directory(parent, error_code))
            return fail("Directory does not exist: " + utf8_from_path(parent));
    }

    const PixelBuffer& source = document.flattened();
    if (source.width() <= 0 || source.height() <= 0) return fail("Nothing to save: " + utf8_from_path(path));

    std::FILE* file = open_file(path, FileMode::Write);
    if (file == nullptr) return fail("Cannot write " + utf8_from_path(path));

    int written = 0;
    if (format == ImageFormat::Png) {
        written = stbi_write_png_to_func(write_stream, file, source.width(), source.height(), 4,
                                         source.row(0).data(), source.width() * 4);
    } else if (format == ImageFormat::Bmp) {
        const std::vector<std::uint8_t> bytes = to_rgb_bytes(flatten_to_opaque(source));
        written = stbi_write_bmp_to_func(write_stream, file, source.width(), source.height(), 3,
                                         bytes.data());
    } else {
        const std::vector<std::uint8_t> bytes = to_rgb_bytes(flatten_to_opaque(source));
        written = stbi_write_jpg_to_func(write_stream, file, source.width(), source.height(), 3,
                                         bytes.data(), kJpegQuality);
    }
    const bool closed = std::fclose(file) == 0;
    if (written == 0 || !closed) return fail("Failed to write " + utf8_from_path(path));
    return true;
}

std::optional<ImageFormat> format_from_extension(const std::filesystem::path& path) {
    const std::string extension = lowered(utf8_from_path(path.extension()));
    if (extension == ".png") return ImageFormat::Png;
    if (extension == ".bmp") return ImageFormat::Bmp;
    if (extension == ".jpg" || extension == ".jpeg") return ImageFormat::Jpeg;
    return std::nullopt;
}

const char* extension_for(ImageFormat format) {
    switch (format) {
        case ImageFormat::Png: return ".png";
        case ImageFormat::Bmp: return ".bmp";
        case ImageFormat::Jpeg: return ".jpg";
    }
    return ".png";
}

PixelBuffer flatten_to_opaque(const PixelBuffer& source, Rgba8 background) {
    PixelBuffer result(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        const Rgba8* pixels = source.row(y).data();
        Rgba8* out = result.row(y).data();
        for (int x = 0; x < source.width(); ++x) {
            const Rgba8 pixel = pixels[x];
            const float alpha = unit_from_byte(pixel.a);
            const auto mix = [alpha](std::uint8_t top, std::uint8_t bottom) {
                return static_cast<std::uint8_t>(std::lround(top * alpha + bottom * (1.0f - alpha)));
            };
            out[x] = Rgba8{mix(pixel.r, background.r), mix(pixel.g, background.g),
                           mix(pixel.b, background.b), 255};
        }
    }
    return result;
}

}  // namespace lsp
