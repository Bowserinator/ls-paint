#pragma once

#include <cstddef>
#include <vector>

#include "core/pixel_buffer.hpp"

namespace lsp {

// The pixels that differ between two versions of a buffer, kept for both
// versions so the change can be applied in either direction. Small changes keep
// copies of the squares that differ; a change that rewrites most of a band of
// rows keeps the band's storage of both versions instead, which costs no copy.
class PixelDelta {
public:
    PixelDelta() = default;

    // Collects what differs between `before` and `after` inside `region`. Both
    // buffers must have the same size.
    [[nodiscard]] static PixelDelta between(const PixelBuffer& before, const PixelBuffer& after,
                                            RectI region);

    [[nodiscard]] bool empty() const noexcept { return m_tiles.empty() && m_bands.empty(); }
    // The area covered by the recorded changes.
    [[nodiscard]] RectI bounds() const noexcept { return m_bounds; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return m_bytes; }

    // Adds the changes of `other`, which must not overlap this delta's.
    void append(PixelDelta&& other);

    void undo(PixelBuffer& target) const;
    void redo(PixelBuffer& target) const;

private:
    struct Tile {
        RectI area;
        PixelBuffer before;
        PixelBuffer after;
    };

    // A whole band of rows, in both versions.
    struct Band {
        int index = 0;
        PixelBuffer::Band before;
        PixelBuffer::Band after;
    };

    [[nodiscard]] std::size_t band_bytes() const;
    static void apply(PixelBuffer& target, const Tile& tile, const PixelBuffer& pixels);
    // Whether `target` splits its rows into bands the way the delta's did.
    [[nodiscard]] bool fits(const PixelBuffer& target, const Band& band) const noexcept;

    std::vector<Tile> m_tiles;
    std::vector<Band> m_bands;
    RectI m_bounds;
    std::size_t m_bytes = 0;
};

}  // namespace lsp
