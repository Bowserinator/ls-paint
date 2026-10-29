#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lsp {

inline constexpr std::uint8_t kChannelMax = 255;
inline constexpr float kChannelMaxFloat = 255.0f;

struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = kChannelMax;

    friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

inline constexpr Rgba8 kWhite{kChannelMax, kChannelMax, kChannelMax, kChannelMax};
inline constexpr Rgba8 kTransparent{0, 0, 0, 0};

// The colour with each of its red, green and blue channels flipped.
[[nodiscard]] constexpr Rgba8 inverted(Rgba8 color) noexcept {
    return {static_cast<std::uint8_t>(kChannelMax - color.r), static_cast<std::uint8_t>(kChannelMax - color.g),
            static_cast<std::uint8_t>(kChannelMax - color.b), color.a};
}

struct Point2f {
    float x = 0;
    float y = 0;
};

// Distance between two points.
[[nodiscard]] inline float distance(Point2f a, Point2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

[[nodiscard]] constexpr Point2f midpoint(Point2f a, Point2f b) noexcept {
    return {(a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f};
}

// The fewest points that enclose an area.
inline constexpr std::size_t kMinPolygonPoints = 3;

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const RectI&, const RectI&) = default;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < right() && py < bottom();
    }

    [[nodiscard]] bool contains(Point2f point) const noexcept {
        return point.x >= static_cast<float>(x) && point.y >= static_cast<float>(y) &&
               point.x < static_cast<float>(right()) && point.y < static_cast<float>(bottom());
    }

    [[nodiscard]] RectI united(const RectI& other) const noexcept {
        if (empty()) return other;
        if (other.empty()) return *this;
        const int left = std::min(x, other.x);
        const int top = std::min(y, other.y);
        return {left, top, std::max(right(), other.right()) - left, std::max(bottom(), other.bottom()) - top};
    }

    [[nodiscard]] RectI intersected(const RectI& other) const noexcept {
        const int left = std::max(x, other.x);
        const int top = std::max(y, other.y);
        const int r = std::min(right(), other.right());
        const int b = std::min(bottom(), other.bottom());
        return r > left && b > top ? RectI{left, top, r - left, b - top} : RectI{};
    }
};

}  // namespace lsp
