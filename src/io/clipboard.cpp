#include "io/clipboard.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "io/document_io.hpp"
#include "utils/env.hpp"

#include <clip.h>

namespace lsp {
namespace {

// The most bytes read from the clipboard in one go.
constexpr std::size_t kMaxClipboardBytes = 256u * 1024u * 1024u;

// Runs `command` and returns its standard output, one byte past the limit at
// most. The reader stops there on purpose: a pipeline that is cut off gets
// EPIPE, so the writer exits instead of blocking for ever on a full pipe.
std::optional<std::string> run_capture(const std::string& command, bool* too_large = nullptr) {
    const std::string limited = command + " | head -c " + std::to_string(kMaxClipboardBytes + 1);
    std::FILE* pipe = popen(limited.c_str(), "r");
    if (pipe == nullptr) return std::nullopt;
    std::string output;
    char buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) output.append(buffer, read);
    const bool succeeded = pclose(pipe) == 0;
    if (output.size() > kMaxClipboardBytes) {
        if (too_large != nullptr) *too_large = true;
        return std::nullopt;
    }
    if (!succeeded) return std::nullopt;
    return output;
}

// A type name reaches the shell, so only the characters of a media type pass.
bool is_safe_type(const std::string& type) {
    return !type.empty() &&
           type.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/+.-") ==
               std::string::npos;
}

bool is_image_type(const std::string& type) { return type.starts_with("image/"); }

// What the Wayland clipboard advertises. Empty without a Wayland session. It is
// read before the X11 bridge, which truncates large images.
std::vector<std::string> wayland_types() {
    if (!env_value("WAYLAND_DISPLAY")) return {};
    const std::optional<std::string> listed = run_capture("wl-paste --list-types");
    if (!listed) return {};

    std::vector<std::string> types;
    std::string type;
    const auto take = [&types, &type] {
        if (!type.empty() && is_safe_type(type) && std::find(types.begin(), types.end(), type) == types.end())
            types.push_back(type);
        type.clear();
    };
    for (char letter : *listed) {
        if (letter == '\n')
            take();
        else if (letter != '\r')
            type.push_back(letter);
    }
    take();
    return types;
}

std::optional<std::string> wayland_bytes(const std::string& type, bool* too_large = nullptr) {
    return run_capture("wl-paste --type " + type, too_large);
}

std::optional<PixelBuffer> wayland_image(std::string* error) {
    bool too_large = false;
    for (const std::string& type : wayland_types()) {
        if (!is_image_type(type)) continue;
        const std::optional<std::string> bytes = wayland_bytes(type, &too_large);
        if (!bytes) continue;
        LoadResult decoded = load_from_memory(bytes->data(), bytes->size());
        if (decoded.ok) return std::move(decoded.pixels);
    }
    if (too_large && error != nullptr)
        *error = "The clipboard image is larger than the " +
                 std::to_string(kMaxClipboardBytes / (1024u * 1024u)) + " MB this program reads.";
    return std::nullopt;
}

std::optional<std::string> wayland_text() {
    const std::vector<std::string> types = wayland_types();
    for (const char* wanted : {"text/plain", "text/uri-list", "text/html"}) {
        if (std::find(types.begin(), types.end(), wanted) == types.end()) continue;
        if (std::optional<std::string> text = wayland_bytes(wanted)) return text;
    }
    return std::nullopt;
}

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
    // A mask that fills the word leaves no room for the scaled value.
    const std::uint64_t full = (std::uint64_t{1} << std::popcount(mask)) - 1u;
    return static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) * 255u / full);
}

}  // namespace

bool Clipboard::set_image(const PixelBuffer& image, RectI region) {
    region = region.intersected(image.bounds());
    if (region.empty()) return false;
    const std::vector<Rgba8> pixels = image.contiguous_copy(region);
    const clip::image wrapped(pixels.data(), rgba8_spec(region.width, region.height));
    return clip::set_image(wrapped);
}

std::optional<PixelBuffer> Clipboard::get_image(std::string* error) {
    if (std::optional<PixelBuffer> image = wayland_image(error)) return image;

    clip::image image;
    if (!clip::get_image(image) || !image.is_valid()) return std::nullopt;

    const clip::image_spec& spec = image.spec();
    if (spec.width == 0 || spec.height == 0) return std::nullopt;
    if (spec.width > static_cast<unsigned long>(kMaxImageDimension) ||
        spec.height > static_cast<unsigned long>(kMaxImageDimension)) {
        if (error != nullptr)
            *error = "The clipboard image is larger than the " +
                     std::to_string(kMaxImageDimension) + " pixel limit.";
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

bool Clipboard::get_text(std::string& text) {
    if (std::optional<std::string> wayland = wayland_text()) {
        text = std::move(*wayland);
        return true;
    }
    return clip::get_text(text);
}

bool Clipboard::has_image() {
    for (const std::string& type : wayland_types()) {
        if (is_image_type(type)) return true;
    }
    return clip::has(clip::image_format());
}

std::vector<std::string> Clipboard::available_types() {
    std::vector<std::string> types = wayland_types();
    if (!types.empty()) return types;
    if (clip::has(clip::image_format())) types.emplace_back("an image (X11)");
    if (clip::has(clip::text_format())) types.emplace_back("text (X11)");
    return types;
}

}  // namespace lsp
