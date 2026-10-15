#pragma once

#include <cstdint>
#include <vector>

#include "core/types.hpp"

namespace lsp {

// Single-channel 1/0 coverage mask over the canvas.
//
// INVARIANT: a mask whose bounds are empty means "no selection", and a no
// selection selects the whole canvas. Every mutation below restores that
// invariant instead of leaving an allocated, all-zero mask behind — such a mask
// used to report `active() == true` while `contains()` was false everywhere,
// which silently clipped every pencil/eraser/fill stroke to nothing while the
// UI showed no selection at all.
class SelectionMask {
public:
    SelectionMask() = default;
    SelectionMask(int width, int height) : m_width(width), m_height(height) {}

    [[nodiscard]] bool active() const noexcept { return !m_mask.empty(); }
    [[nodiscard]] bool empty() const noexcept { return m_bounds.empty(); }
    [[nodiscard]] RectI bounds() const noexcept { return m_bounds; }

    friend bool operator==(const SelectionMask&, const SelectionMask&) = default;

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        if (m_mask.empty() || m_bounds.empty()) return true;
        return x >= 0 && y >= 0 && x < m_width && y < m_height &&
               m_mask[static_cast<std::size_t>(y) * m_width + x] != 0;
    }

    void clear() { m_mask.clear(); m_bounds = {}; }

    void fill_rect(RectI rect);
    void fill_polygon(const std::vector<Point2f>& points);
    void invert();

    // Moves every selected pixel by (dx, dy); pixels leaving the canvas are
    // dropped. Used while dragging a selection's contents.
    void translate(int dx, int dy);

    // Overwrites the mask inside `region` with `bytes` (laid out for the
    // unclipped region, row-major, 0/1) and recomputes the bounds.
    void replace_region(RectI region, const std::vector<std::uint8_t>& bytes);

    // Reads the mask inside `region` as 0/1 bytes laid out for the unclipped
    // region; positions outside the canvas read as 0.
    [[nodiscard]] std::vector<std::uint8_t> extract_region(RectI region) const;

private:
    void ensure_allocated();
    void recompute_bounds();

    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_mask;
    RectI m_bounds;
};

}  // namespace lsp
