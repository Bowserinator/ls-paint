#pragma once

#include <optional>

#include "brushes/brush.hpp"
#include "core/pixel_buffer.hpp"
#include "selection/selection_mask.hpp"

namespace lsp {

class BrushEngine {
public:
    BrushEngine(const Brush& brush, Rgba8 color, const SelectionMask& selection)
        : m_brush(brush), m_color(color), m_selection(selection), m_profile(effective_profile(brush)) {}

    RectI stroke_to(PixelBuffer& target, Point2f to);

private:
    // The nib this stroke lays down. The plain brush takes its tip and its
    // stamp spacing from the tool options; every other type brings its own.
    [[nodiscard]] static BrushProfile effective_profile(const Brush& brush);

    RectI stamp(PixelBuffer& target, Point2f center) const;

    Brush m_brush;
    Rgba8 m_color;
    const SelectionMask& m_selection;
    BrushProfile m_profile;
    std::optional<Point2f> m_last;
    float m_carry = 0.0f;
};

}  // namespace lsp
