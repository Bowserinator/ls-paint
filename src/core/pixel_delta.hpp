#pragma once

#include <cstddef>
#include <vector>

#include "core/pixel_buffer.hpp"

namespace lsp {

// The pixels that differ between two versions of a buffer, kept for both
// versions so the change can be applied in either direction.
class PixelDelta {
public:
    PixelDelta() = default;

    // Collects what differs between `before` and `after` inside `region`. Both
    // buffers must have the same size.
    [[nodiscard]] static PixelDelta between(const PixelBuffer& before, const PixelBuffer& after,
                                            RectI region);

    [[nodiscard]] bool empty() const noexcept { return m_tiles.empty(); }
    // The area covered by the recorded changes.
    [[nodiscard]] RectI bounds() const noexcept { return m_bounds; }
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return m_bytes; }

    // Adds the changes of `other`, which must not overlap this delta's.
    void append(PixelDelta&& other);

    // Moves everything the delta describes by (dx, dy).
    void translate(int dx, int dy);

    void undo(PixelBuffer& target) const;
    void redo(PixelBuffer& target) const;

private:
    struct Tile {
        RectI area;
        PixelBuffer before;
        PixelBuffer after;
    };

    static void apply(PixelBuffer& target, const Tile& tile, const PixelBuffer& pixels);

    std::vector<Tile> m_tiles;
    RectI m_bounds;
    std::size_t m_bytes = 0;
};

}  // namespace lsp
