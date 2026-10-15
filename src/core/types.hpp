#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lsp {

struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const Rgba8&, const Rgba8&) = default;
};

struct Point2f {
    float x = 0;
    float y = 0;
};

// Distance between two points, used to decide whether a click is close enough
// to a handle or a vertex.
[[nodiscard]] inline float distance(Point2f a, Point2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const RectI&, const RectI&) = default;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }

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
