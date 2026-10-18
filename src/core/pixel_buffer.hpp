#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/types.hpp"

namespace lsp {

class PixelBuffer {
public:
    PixelBuffer() = default;

    PixelBuffer(int width, int height, Rgba8 fill = {0, 0, 0, 0})
        : m_width(width), m_height(height), m_pixels(static_cast<std::size_t>(width) * height, fill) {}

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] RectI bounds() const noexcept { return {0, 0, m_width, m_height}; }

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < m_width && y < m_height;
    }

    [[nodiscard]] std::span<Rgba8> row(int y) {
        return {m_pixels.data() + static_cast<std::size_t>(y) * m_width, static_cast<std::size_t>(m_width)};
    }

    [[nodiscard]] std::span<const Rgba8> row(int y) const {
        return {m_pixels.data() + static_cast<std::size_t>(y) * m_width, static_cast<std::size_t>(m_width)};
    }

    [[nodiscard]] Rgba8& at(int x, int y) { return row(y)[static_cast<std::size_t>(x)]; }
    [[nodiscard]] const Rgba8& at(int x, int y) const { return row(y)[static_cast<std::size_t>(x)]; }

    friend bool operator==(const PixelBuffer&, const PixelBuffer&) = default;

    [[nodiscard]] PixelBuffer copy_region(RectI region) const;
    void blit(int x, int y, const PixelBuffer& source);

    [[nodiscard]] PixelBuffer rotated_90_cw() const;
    [[nodiscard]] PixelBuffer flipped_horizontal() const;
    [[nodiscard]] PixelBuffer flipped_vertical() const;

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<Rgba8> m_pixels;
};

// Bytes one buffer of this size holds, for callers that budget their own
// memory: the undo stack measures its entries with this.
[[nodiscard]] inline std::size_t pixel_bytes(const PixelBuffer& pixels) noexcept {
    return static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()) *
           sizeof(Rgba8);
}

}  // namespace lsp
