#include "core/color.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace lsp {

float wrap_hue(float hue) noexcept {
    return std::fmod(std::fmod(hue, 360.0f) + 360.0f, 360.0f);
}

namespace {

std::uint8_t to_byte(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

int hex_value(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return digit - 'A' + 10;
}

std::optional<Rgba8> parse_hex(std::string_view digits) {
    if (digits.size() != 3 && digits.size() != 6) return std::nullopt;
    for (const char digit : digits) {
        if (std::isxdigit(static_cast<unsigned char>(digit)) == 0) return std::nullopt;
    }
    if (digits.size() == 3) {
        return Rgba8{static_cast<std::uint8_t>(hex_value(digits[0]) * 17),
                     static_cast<std::uint8_t>(hex_value(digits[1]) * 17),
                     static_cast<std::uint8_t>(hex_value(digits[2]) * 17), 255};
    }
    return Rgba8{static_cast<std::uint8_t>(hex_value(digits[0]) * 16 + hex_value(digits[1])),
                 static_cast<std::uint8_t>(hex_value(digits[2]) * 16 + hex_value(digits[3])),
                 static_cast<std::uint8_t>(hex_value(digits[4]) * 16 + hex_value(digits[5])), 255};
}

std::optional<Rgba8> parse_tuple(std::string_view text) {
    if (text.empty() || text.back() == ',') return std::nullopt;
    Rgba8 color{0, 0, 0, 255};
    std::uint8_t* fields[3] = {&color.r, &color.g, &color.b};
    std::size_t index = 0;
    while (!text.empty()) {
        if (index == 3) return std::nullopt;
        const std::size_t comma = text.find(',');
        const std::string_view field = trim(text.substr(0, comma));
        int value = 0;
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value);
        if (error != std::errc{} || end != field.data() + field.size() || value < 0 || value > 255) {
            return std::nullopt;
        }
        *fields[index] = static_cast<std::uint8_t>(value);
        ++index;
        if (comma == std::string_view::npos) {
            text = {};
        } else {
            text.remove_prefix(comma + 1);
        }
    }
    if (index != 3) return std::nullopt;
    return color;
}

}  // namespace

Hsv hsv_from_rgb(Rgba8 color) noexcept {
    const float red = static_cast<float>(color.r) / 255.0f;
    const float green = static_cast<float>(color.g) / 255.0f;
    const float blue = static_cast<float>(color.b) / 255.0f;
    const float high = std::max({red, green, blue});
    const float low = std::min({red, green, blue});
    const float span = high - low;

    Hsv result;
    result.value = high;
    result.saturation = high > 0.0f ? span / high : 0.0f;
    if (span > 0.0f) {
        if (high == red) {
            result.hue = 60.0f * std::fmod((green - blue) / span, 6.0f);
        } else if (high == green) {
            result.hue = 60.0f * ((blue - red) / span + 2.0f);
        } else {
            result.hue = 60.0f * ((red - green) / span + 4.0f);
        }
        result.hue = wrap_hue(result.hue);
    }
    return result;
}

Rgba8 rgb_from_hsv(const Hsv& color) noexcept {
    const float hue = wrap_hue(color.hue);
    const float saturation = std::clamp(color.saturation, 0.0f, 1.0f);
    const float value = std::clamp(color.value, 0.0f, 1.0f);
    const float chroma = value * saturation;
    const float section = hue / 60.0f;
    const float second = chroma * (1.0f - std::fabs(std::fmod(section, 2.0f) - 1.0f));

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    switch (static_cast<int>(section) % 6) {
        case 0: red = chroma; green = second; break;
        case 1: red = second; green = chroma; break;
        case 2: green = chroma; blue = second; break;
        case 3: green = second; blue = chroma; break;
        case 4: red = second; blue = chroma; break;
        default: red = chroma; blue = second; break;
    }
    const float base = value - chroma;
    return Rgba8{to_byte(red + base), to_byte(green + base), to_byte(blue + base), 255};
}

std::string to_hex(Rgba8 color) {
    char text[8] = {};
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", color.r, color.g, color.b);
    return text;
}

std::string clean_hex_entry(std::string_view text) {
    const std::string_view body = trim(text);
    if (body.empty() || body.front() != '#') return std::string(text);
    const std::string_view digits = trim(body.substr(1));
    if (digits.size() == 3 && parse_hex(digits)) {
        std::string doubled;
        for (const char digit : digits) {
            doubled += digit;
            doubled += digit;
        }
        return doubled;
    }
    return std::string(digits);
}

std::optional<Rgba8> parse_color(std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;
    if (text.front() == '#') return parse_hex(trim(text.substr(1)));
    if (text.find(',') != std::string_view::npos) return parse_tuple(text);
    return parse_hex(text);
}

}  // namespace lsp
