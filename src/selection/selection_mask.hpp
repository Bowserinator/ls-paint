#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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

    [[nodiscard]] bool active() const noexcept { return m_mask != nullptr; }
    [[nodiscard]] bool empty() const noexcept { return m_bounds.empty(); }
    [[nodiscard]] RectI bounds() const noexcept { return m_bounds; }

    friend bool operator==(const SelectionMask& a, const SelectionMask& b);

    [[nodiscard]] bool contains(int x, int y) const noexcept {
        if (!m_mask || m_bounds.empty()) return true;
        return x >= 0 && y >= 0 && x < m_width && y < m_height && cell(x, y) != 0;
    }

    void clear() {
        m_mask.reset();
        m_bounds = {};
    }

    // Bytes the mask holds.
    [[nodiscard]] std::size_t memory_bytes() const noexcept { return m_mask ? m_mask->size() : 0; }

    // Keeps only the cells that `other` also selects. A mask with no
    // selection leaves everything as it is.
    void intersect(const SelectionMask& other);

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
    // The cells to write to: allocated when missing and unshared from any copy.
    [[nodiscard]] std::vector<std::uint8_t>& writable();
    // Replaces the cells with a fresh, all-clear set.
    void reset_cells();
    void recompute_bounds();
    [[nodiscard]] std::uint8_t& cell(int x, int y);
    [[nodiscard]] std::uint8_t cell(int x, int y) const;

    int m_width = 0;
    int m_height = 0;
    // Shared between copies until one of them is written to.
    std::shared_ptr<std::vector<std::uint8_t>> m_mask;
    RectI m_bounds;
};

}  // namespace lsp
