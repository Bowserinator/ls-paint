#pragma once

#include <bit>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/types.hpp"
#include "utils/math.hpp"

namespace lsp {

// A grid of pixels. Storage is split into horizontal bands that are shared
// between copies until one of them is written to, so copying a buffer is cheap
// and a fresh uniform buffer only costs memory where it is later painted.
//
// Spans handed out by row() stay valid only until the buffer is copied or
// assigned.
class PixelBuffer {
public:
    PixelBuffer() = default;
    PixelBuffer(int width, int height, Rgba8 fill = kTransparent);

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] RectI bounds() const noexcept { return {0, 0, m_width, m_height}; }

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < m_width && y < m_height;
    }

    [[nodiscard]] std::span<Rgba8> row(int y) {
        Band& band = m_bands[static_cast<std::size_t>(y) >> m_band_shift];
        if (band.use_count() > 1) band = std::make_shared<std::vector<Rgba8>>(*band);
        return {band->data() + row_offset(y), static_cast<std::size_t>(m_width)};
    }

    [[nodiscard]] std::span<const Rgba8> row(int y) const {
        const Band& band = m_bands[static_cast<std::size_t>(y) >> m_band_shift];
        return {band->data() + row_offset(y), static_cast<std::size_t>(m_width)};
    }

    [[nodiscard]] Rgba8& at(int x, int y) { return row(y)[static_cast<std::size_t>(x)]; }
    [[nodiscard]] const Rgba8& at(int x, int y) const { return row(y)[static_cast<std::size_t>(x)]; }

    friend bool operator==(const PixelBuffer& a, const PixelBuffer& b);

    [[nodiscard]] PixelBuffer copy_region(RectI region) const;
    void blit(int x, int y, const PixelBuffer& source);
    // Copies `region` from a buffer of the same size.
    void copy_region_from(const PixelBuffer& source, RectI region);
    // Whether row `y` of both buffers is known to hold the same pixels without
    // looking at them.
    [[nodiscard]] bool row_shared_with(const PixelBuffer& other, int y) const noexcept;
    // The pixels of `region` as one row-major block.
    [[nodiscard]] std::vector<Rgba8> contiguous_copy(RectI region) const;
    // Bytes of pixel storage the buffer references.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

    [[nodiscard]] PixelBuffer rotated_90_cw() const;
    [[nodiscard]] PixelBuffer rotated_90_ccw() const;
    [[nodiscard]] PixelBuffer rotated_180() const;
    [[nodiscard]] PixelBuffer flipped_horizontal() const;
    [[nodiscard]] PixelBuffer flipped_vertical() const;

private:
    using Band = std::shared_ptr<std::vector<Rgba8>>;

    [[nodiscard]] std::size_t row_offset(int y) const noexcept {
        return (static_cast<std::size_t>(y) & m_band_mask) * static_cast<std::size_t>(m_width);
    }

    int m_width = 0;
    int m_height = 0;
    unsigned m_band_shift = 0;
    std::size_t m_band_mask = 0;
    std::vector<Band> m_bands;
};

// Bytes one buffer of this size holds.
[[nodiscard]] inline std::size_t pixel_bytes(const PixelBuffer& pixels) noexcept {
    return pixels.storage_bytes();
}

}  // namespace lsp
