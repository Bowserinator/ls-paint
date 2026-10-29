#include "core/color.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "utils/math.hpp"
#include "utils/text.hpp"

namespace lsp {

float wrap_hue(float hue) noexcept {
    return std::fmod(std::fmod(hue, kFullTurnDegrees) + kFullTurnDegrees, kFullTurnDegrees);
}

namespace {

constexpr std::size_t kRgbComponents = 3;
constexpr float kHueSectorDegrees = 60.0f;

int hex_value(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return digit - 'A' + 10;
}

std::uint8_t hex_byte(char high, char low) {
    return static_cast<std::uint8_t>(hex_value(high) * 16 + hex_value(low));
}

std::optional<Rgba8> parse_hex(std::string_view digits) {
    if (digits.size() != kShortHexDigits && digits.size() != kLongHexDigits) return std::nullopt;
    for (const char digit : digits) {
        if (std::isxdigit(static_cast<unsigned char>(digit)) == 0) return std::nullopt;
    }
    if (digits.size() == kShortHexDigits) {
        return Rgba8{hex_byte(digits[0], digits[0]), hex_byte(digits[1], digits[1]),
                     hex_byte(digits[2], digits[2]), kChannelMax};
    }
    return Rgba8{hex_byte(digits[0], digits[1]), hex_byte(digits[2], digits[3]),
                 hex_byte(digits[4], digits[5]), kChannelMax};
}

std::optional<Rgba8> parse_tuple(std::string_view text) {
    if (text.empty() || text.back() == ',') return std::nullopt;
    Rgba8 color{0, 0, 0, kChannelMax};
    std::uint8_t* fields[kRgbComponents] = {&color.r, &color.g, &color.b};
    std::size_t index = 0;
    while (!text.empty()) {
        if (index == kRgbComponents) return std::nullopt;
        const std::size_t comma = text.find(',');
        const std::optional<int> value = parse_int(trimmed(text.substr(0, comma)));
        if (!value || *value < 0 || *value > kChannelMax) return std::nullopt;
        *fields[index] = static_cast<std::uint8_t>(*value);
        ++index;
        if (comma == std::string_view::npos) {
            text = {};
        } else {
            text.remove_prefix(comma + 1);
        }
    }
    if (index != kRgbComponents) return std::nullopt;
    return color;
}

}  // namespace

Hsv hsv_from_rgb(Rgba8 color) noexcept {
    const float red = unit_from_byte(color.r);
    const float green = unit_from_byte(color.g);
    const float blue = unit_from_byte(color.b);
    const float high = std::max({red, green, blue});
    const float low = std::min({red, green, blue});
    const float span = high - low;

    Hsv result;
    result.value = high;
    result.saturation = high > 0.0f ? span / high : 0.0f;
    if (span > 0.0f) {
        if (high == red) {
            result.hue = kHueSectorDegrees * std::fmod((green - blue) / span, 6.0f);
        } else if (high == green) {
            result.hue = kHueSectorDegrees * ((blue - red) / span + 2.0f);
        } else {
            result.hue = kHueSectorDegrees * ((red - green) / span + 4.0f);
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
    const float section = hue / kHueSectorDegrees;
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
    return Rgba8{byte_from_unit(red + base), byte_from_unit(green + base), byte_from_unit(blue + base),
                 kChannelMax};
}

std::string to_hex(Rgba8 color) {
    char text[8] = {};
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", color.r, color.g, color.b);
    return text;
}

std::string clean_hex_entry(std::string_view text) {
    const std::string_view body = trimmed(text);
    if (body.empty() || body.front() != '#') return std::string(text);
    const std::string_view digits = trimmed(body.substr(1));
    if (digits.size() == kShortHexDigits && parse_hex(digits)) {
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
    text = trimmed(text);
    if (text.empty()) return std::nullopt;
    if (text.front() == '#') return parse_hex(trimmed(text.substr(1)));
    if (text.find(',') != std::string_view::npos) return parse_tuple(text);
    return parse_hex(text);
}

}  // namespace lsp
