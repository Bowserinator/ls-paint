#pragma once

#include <vector>

#include "core/pixel_buffer.hpp"
#include "core/types.hpp"

namespace lsp {

// A block of pixels in row-major order.
struct PixelGrid {
    int width = 0;
    int height = 0;
    std::vector<Rgba8> pixels;
};

// The screen pixels a tile image fills, counted from the view's origin.
struct DeviceBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const DeviceBox&, const DeviceBox&) = default;
};

// The smallest box that holds both `a` and `b`.
[[nodiscard]] DeviceBox united(const DeviceBox& a, const DeviceBox& b);

// The area averaged down by `factor`, each pixel weighing its colour by its
// alpha so transparent pixels do not darken the result.
[[nodiscard]] PixelGrid box_reduced(const PixelBuffer& source, RectI area, int factor);

// How many times the canvas is halved for the quick view at `scale` screen
// pixels per document pixel, which is the power of two that lands nearest 1:1.
[[nodiscard]] int quick_level(double scale);

// How many times the canvas is halved before `resampled` filters it.
[[nodiscard]] int resample_level(double scale);

// Document pixels each tile image shows past its edge once resampled.
[[nodiscard]] float resample_overlap(double scale);

// Where a document coordinate lands on screen, in device pixels. Edges sit on
// whole device pixels, except the canvas's own far edge, which stays exact.
[[nodiscard]] float tile_edge(float doc, float zoom, float dpr, int extent);

// The screen pixels the image of one tile fills when drawn at `zoom` on a
// screen with `dpr` device pixels per logical pixel.
[[nodiscard]] DeviceBox tile_device_box(int column, int row, int tile_size, float overlap, float zoom,
                                        float dpr, int canvas_width, int canvas_height);

// The part of `grid`, which shows the screen pixels under `outer`, that lies
// under `part`. `part` must lie within `outer`.
[[nodiscard]] PixelGrid cropped(const PixelGrid& grid, const DeviceBox& outer, const DeviceBox& part);

// The canvas seen through `box` at `scale` screen pixels per document pixel,
// one output pixel per screen pixel. Screen pixel n shows the document around
// n / scale however large a box it is asked for, so a box covering several
// tiles can be cut into them with no seam between.
[[nodiscard]] PixelGrid resampled(const PixelBuffer& source, DeviceBox box, double scale);

// The canvas seen through `box` with each screen pixel taking the one pixel of
// the canvas, halved `level` times, that lies under its centre. Like
// `resampled`, a screen pixel shows the same thing however large a box is asked.
[[nodiscard]] PixelGrid sampled(const PixelBuffer& source, DeviceBox box, double scale, int level);

}  // namespace lsp
