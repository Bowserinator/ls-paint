#pragma once

#include <array>
#include <optional>

#include "core/types.hpp"

namespace lsp {

// The eight resize grips of a box: four corners plus the middle of every edge.
enum class SelectionHandle { Nw, N, Ne, E, Se, S, Sw, W };

// The grips in the order a press looks for them: corners first, so a small
// box prefers them over the edge grips.
inline constexpr std::array kSelectionHandles{SelectionHandle::Nw, SelectionHandle::Ne,
                                              SelectionHandle::Se, SelectionHandle::Sw,
                                              SelectionHandle::N,  SelectionHandle::E,
                                              SelectionHandle::S,  SelectionHandle::W};

// Resize grips are only offered on boxes at least this many pixels across, so
// the grips do not swallow the "drag inside to move it" target.
inline constexpr int kMinHandleSpan = 8;

// How close, in screen pixels, a pointer has to be to a grip to take it: the
// reach of the grip as drawn, whatever the zoom.
inline constexpr float kHandleHitRadius = 3.5f;

// The steps Shift snaps a turn to.
inline constexpr float kRotateStepDegrees = 45.0f;

// The pointer shape that goes with a resize grip, by the line the grip drags
// along.
enum class ResizeCursor { None, Horizontal, Vertical, DiagonalNwSe, DiagonalNeSw };

// Where a grip sits: on the border pixels of `bounds`.
[[nodiscard]] Point2f handle_anchor(RectI bounds, SelectionHandle handle);

// The grip a press at `position` takes, if any. `zoom` is screen pixels per
// document pixel.
[[nodiscard]] std::optional<SelectionHandle> handle_at(RectI bounds, Point2f position, float zoom);

// True when a press at `position` would start a turn: near a corner, but not on
// a resize grip or inside the box, which take the press first.
[[nodiscard]] bool in_rotate_zone(RectI bounds, Point2f position, float zoom);

// The pointer's bearing from a point, in degrees. Canvas y runs down, so a
// growing bearing is a turn to the right.
[[nodiscard]] float bearing_degrees(Point2f centre, Point2f position);

// The pointer shape that goes with a grip.
[[nodiscard]] ResizeCursor resize_cursor_for(SelectionHandle handle);

// The rectangle dragging `handle` to `position` produces: the edges opposite
// the grip stay put, and the result stays on `canvas` at least one pixel wide.
// With `keep_aspect` the result has the proportions of `start` instead, however
// the pointer wanders, so a drag never drifts from the shape it began with.
[[nodiscard]] RectI resized_bounds(RectI start, SelectionHandle handle, Point2f position, RectI canvas,
                                   bool keep_aspect = false);

}  // namespace lsp
