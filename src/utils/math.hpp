#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "core/types.hpp"

namespace lsp {

inline constexpr float kQuarterTurnDegrees = 90.0f;
inline constexpr float kHalfTurnDegrees = 180.0f;
inline constexpr float kThreeQuarterTurnDegrees = 270.0f;
inline constexpr float kFullTurnDegrees = 360.0f;

[[nodiscard]] inline int round_to_int(float value) { return static_cast<int>(std::lround(value)); }
[[nodiscard]] inline int floor_to_int(float value) { return static_cast<int>(std::floor(value)); }
[[nodiscard]] inline int ceil_to_int(float value) { return static_cast<int>(std::ceil(value)); }

[[nodiscard]] constexpr float radians_from_degrees(float degrees) noexcept {
    return degrees * std::numbers::pi_v<float> / kHalfTurnDegrees;
}

[[nodiscard]] constexpr float degrees_from_radians(float radians) noexcept {
    return radians * kHalfTurnDegrees / std::numbers::pi_v<float>;
}

// A turn as the pair of values that apply it to a point.
struct Turn {
    float cosine = 1.0f;
    float sine = 0.0f;
};

// The turn by `radians`, with values within rounding noise of zero set to
// exactly zero so a quarter turn lands on whole pixels.
[[nodiscard]] inline Turn turn_by(float radians) {
    constexpr float kNoise = 1e-4f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {std::abs(cosine) < kNoise ? 0.0f : cosine, std::abs(sine) < kNoise ? 0.0f : sine};
}

// The offset of (x, y) in a row-major grid `width` cells wide.
[[nodiscard]] constexpr std::size_t grid_index(int x, int y, int width) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

// The centre of the pixel at `index` along an axis.
[[nodiscard]] constexpr float pixel_center(int index) noexcept { return static_cast<float>(index) + 0.5f; }

// How many `divisor`-sized pieces it takes to cover `value`.
template <std::integral T>
[[nodiscard]] constexpr T ceil_div(T value, T divisor) noexcept {
    return (value + divisor - 1) / divisor;
}

// The cell that the cell at `index` of an axis of `size` cells copies when the
// axis is stretched or squashed from `source_size` cells.
[[nodiscard]] constexpr int nearest_source_index(int index, int source_size, int size) noexcept {
    return std::min(source_size - 1, static_cast<int>(static_cast<long long>(index) * source_size / size));
}

// Maps a channel between the 0..255 byte range and the 0..1 range.
[[nodiscard]] constexpr float unit_from_byte(std::uint8_t value) noexcept {
    return static_cast<float>(value) / kChannelMaxFloat;
}

[[nodiscard]] inline std::uint8_t byte_from_unit(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * kChannelMaxFloat));
}

}  // namespace lsp
