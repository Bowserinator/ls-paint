#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace lsp {

// The digits of a colour written in the short "rgb" and the long "rrggbb" hex form.
inline constexpr std::size_t kShortHexDigits = 3;
inline constexpr std::size_t kLongHexDigits = 6;

// A colour as hue, saturation and value: hue in degrees, the other two in [0, 1].
// The Edit Colors picker arranges its ring and square in this space.
struct Hsv {
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;

    friend bool operator==(const Hsv&, const Hsv&) = default;
};

// Alpha is ignored: every colour the picker shows is opaque.
[[nodiscard]] Hsv hsv_from_rgb(Rgba8 color) noexcept;
[[nodiscard]] Rgba8 rgb_from_hsv(const Hsv& color) noexcept;

// Any hue, wrapped or negative, lands in [0, 360).
[[nodiscard]] float wrap_hue(float hue) noexcept;

// Tidies text that was typed or pasted into a hex box. A leading "#" is dropped,
// and "#rgb" becomes "rrggbb". Text with no "#" comes back as it was, so a box
// that applies itself as the digits arrive is not disturbed mid-typing.
[[nodiscard]] std::string clean_hex_entry(std::string_view text);

// The colour as "#rrggbb", lower case.
[[nodiscard]] std::string to_hex(Rgba8 color);

// Reads a colour written as "#rrggbb", "#rgb", "rrggbb", "rgb", or as an
// "r,g,b" tuple whose values are 0 to 255. Returns nothing for anything else, so
// a caller can leave the current colour alone.
[[nodiscard]] std::optional<Rgba8> parse_color(std::string_view text);

}  // namespace lsp
