#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/pixel_buffer.hpp"
#include "core/types.hpp"
#include "selection/selection_mask.hpp"

// Hard-edged raster primitives shared by the shape and text tools.
//
// Every primitive takes a Target, which is the pixel buffer it paints into plus
// an optional coverage mask for the current selection. Nothing here allocates,
// anti-aliases or knows about the document, so the geometry-driven tools can be
// previewed and unit-tested without a window.
namespace lsp::raster {

struct Target {
    PixelBuffer& pixels;
    // The selection that holds painting back, sized like `pixels`. Null means
    // the whole canvas is writable.
    const SelectionMask* clip = nullptr;

    [[nodiscard]] bool allows(int x, int y) const noexcept;
};

void draw_line(Target target, Point2f a, Point2f b, int thickness, Rgba8 color);
void draw_rect_outline(Target target, RectI rect, int thickness, Rgba8 color);
void draw_rect_fill(Target target, RectI rect, Rgba8 color);
void draw_rounded_rect_outline(Target target, RectI rect, int radius, int thickness, Rgba8 color);
void draw_rounded_rect_fill(Target target, RectI rect, int radius, Rgba8 color);
void draw_ellipse_outline(Target target, RectI box, int thickness, Rgba8 color);
void draw_ellipse_fill(Target target, RectI box, Rgba8 color);
void draw_polyline(Target target, std::span<const Point2f> points, int thickness, Rgba8 color,
                   bool closed);
void draw_polygon_fill(Target target, std::span<const Point2f> points, Rgba8 color);
// Stamps an 8-bit coverage bitmap (one byte per pixel, `stride` bytes per row)
// with `coverage` scaling the color's alpha, so glyph edges stay smooth.
void draw_glyph(Target target, int x, int y, int width, int height, int stride,
                const std::uint8_t* coverage, Rgba8 color);

[[nodiscard]] RectI bounds_rect(RectI rect, int thickness);
[[nodiscard]] RectI bounds_points(std::span<const Point2f> points, int thickness);

}  // namespace lsp::raster
