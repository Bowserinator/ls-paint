#pragma once

#include <cstdint>

#include "core/pixel_buffer.hpp"
#include "core/types.hpp"

// The marks the pointer draws over the paper: the outline of the brush tip it
// would stamp and a crosshair. Every mark pixel takes the paper colour under it
// with the channels inverted, so the marks read on any paper.
namespace lsp {

// The outline of the tip, in screen pixels.
struct CursorTip {
    bool shown = false;
    // Square corners instead of a rounded outline.
    bool square = false;
    // Half the tip's size, and how far its centre sits from the pointer.
    float half_width = 0.0F;
    float half_height = 0.0F;
    float offset_x = 0.0F;
    float offset_y = 0.0F;
    // Tilt clockwise from upright, for a calligraphy nib.
    float rotation_degrees = 0.0F;
};

// Everything the pointer draws, with the pointer at `pointer` in document
// pixels and `zoom` screen pixels per document pixel.
struct CursorShape {
    CursorTip tip;
    bool crosshair = false;
    Point2f pointer;
    float zoom = 1.0F;
};

struct CursorOverlay {
    // Half the side of the square the marks fit in, in screen pixels.
    int reach = 0;
    // The marks, transparent where nothing is drawn.
    PixelBuffer pixels;
};

// Renders the marks of `shape` onto a transparent square around the pointer.
// `around` colours the marks that fall past the edge of the paper.
[[nodiscard]] CursorOverlay render_cursor_overlay(const PixelBuffer& paper, const CursorShape& shape,
                                                 Rgba8 around);

}  // namespace lsp
