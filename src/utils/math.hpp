#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace lsp {

[[nodiscard]] inline int round_to_int(float value) { return static_cast<int>(std::lround(value)); }
[[nodiscard]] inline int floor_to_int(float value) { return static_cast<int>(std::floor(value)); }
[[nodiscard]] inline int ceil_to_int(float value) { return static_cast<int>(std::ceil(value)); }

[[nodiscard]] constexpr float radians_from_degrees(float degrees) noexcept {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

[[nodiscard]] constexpr float degrees_from_radians(float radians) noexcept {
    return radians * 180.0f / std::numbers::pi_v<float>;
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

// Maps a channel between the 0..255 byte range and the 0..1 range.
[[nodiscard]] constexpr float unit_from_byte(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0f;
}

[[nodiscard]] inline std::uint8_t byte_from_unit(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

}  // namespace lsp
