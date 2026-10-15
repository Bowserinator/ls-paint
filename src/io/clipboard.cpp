#include "io/clipboard.hpp"

#include <bit>
#include <cstdint>
#include <cstring>

#include "config.hpp"

#include <clip.h>

namespace lsp {
namespace {

// One little-endian Rgba8 word: red in the low byte, alpha in the high byte.
clip::image_spec rgba8_spec(int width, int height) {
    clip::image_spec spec;
    spec.width = static_cast<unsigned long>(width);
    spec.height = static_cast<unsigned long>(height);
    spec.bits_per_pixel = 32;
    spec.bytes_per_row = static_cast<unsigned long>(width) * 4;
    spec.red_mask = 0x000000ff;
    spec.green_mask = 0x0000ff00;
    spec.blue_mask = 0x00ff0000;
    spec.alpha_mask = 0xff000000;
    spec.red_shift = 0;
    spec.green_shift = 8;
    spec.blue_shift = 16;
    spec.alpha_shift = 24;
    return spec;
}

std::uint32_t read_word(const unsigned char* pixel, int bytes) {
    std::uint32_t word = 0;
    for (int i = 0; i < bytes && i < 4; ++i) word |= static_cast<std::uint32_t>(pixel[i]) << (8 * i);
    return word;
}

// Pulls one masked channel out of a pixel word and scales it to eight bits.
std::uint8_t read_channel(std::uint32_t word, unsigned long mask, unsigned long shift) {
    if (mask == 0) return 255;
    const std::uint32_t value = (word & static_cast<std::uint32_t>(mask)) >> shift;
    const unsigned long bits = std::popcount(mask);
    const std::uint32_t full = (1u << bits) - 1u;
    return static_cast<std::uint8_t>(value * 255u / full);
}

}  // namespace

bool Clipboard::set_image(const PixelBuffer& image) {
    if (image.width() <= 0 || image.height() <= 0) return false;
    const clip::image wrapped(image.row(0).data(), rgba8_spec(image.width(), image.height()));
    return clip::set_image(wrapped);
}

std::optional<PixelBuffer> Clipboard::get_image() {
    clip::image image;
    if (!clip::get_image(image) || !image.is_valid()) return std::nullopt;

    const clip::image_spec& spec = image.spec();
    if (spec.width == 0 || spec.height == 0) return std::nullopt;
    if (spec.width > static_cast<unsigned long>(kMaxImageDimension) ||
        spec.height > static_cast<unsigned long>(kMaxImageDimension)) {
        return std::nullopt;
    }

    const int bytes = static_cast<int>(spec.bits_per_pixel / 8);
    PixelBuffer pixels(static_cast<int>(spec.width), static_cast<int>(spec.height));
    const unsigned char* data = reinterpret_cast<const unsigned char*>(image.data());
    for (std::size_t y = 0; y < spec.height; ++y) {
        const unsigned char* row = data + y * spec.bytes_per_row;
        Rgba8* out = pixels.row(static_cast<int>(y)).data();
        for (std::size_t x = 0; x < spec.width; ++x) {
            const std::uint32_t word = read_word(row + x * static_cast<std::size_t>(bytes), bytes);
            out[x] = Rgba8{read_channel(word, spec.red_mask, spec.red_shift),
                           read_channel(word, spec.green_mask, spec.green_shift),
                           read_channel(word, spec.blue_mask, spec.blue_shift),
                           read_channel(word, spec.alpha_mask, spec.alpha_shift)};
        }
    }
    return pixels;
}

}  // namespace lsp
