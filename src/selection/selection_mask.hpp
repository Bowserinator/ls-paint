#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/types.hpp"

namespace lsp {

// A 1/0 coverage mask over the canvas.
//
// A mask with empty bounds means "no selection", and a canvas with no
// selection is writable everywhere.
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
        return x >= 0 && y >= 0 && x < m_width && y < m_height && cell(x, y) != 0;
    }

    void clear() { m_mask.clear(); m_bounds = {}; }

    void fill_rect(RectI rect);
    void fill_polygon(std::span<const Point2f> points);
    void invert();

    // Overwrites the mask inside `region` with `bytes` (laid out for the
    // unclipped region, row-major, 0/1) and recomputes the bounds.
    void replace_region(RectI region, const std::vector<std::uint8_t>& bytes);

    // Reads the mask inside `region` as 0/1 bytes laid out for the unclipped
    // region; positions outside the canvas read as 0.
    [[nodiscard]] std::vector<std::uint8_t> extract_region(RectI region) const;

private:
    void ensure_allocated();
    void recompute_bounds();
    [[nodiscard]] std::uint8_t& cell(int x, int y);
    [[nodiscard]] std::uint8_t cell(int x, int y) const;

    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_mask;
    RectI m_bounds;
};

}  // namespace lsp
