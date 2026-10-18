#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/pixel_buffer.hpp"
#include "selection/selection_mask.hpp"

namespace lsp {

void make_transparent(PixelBuffer& pixels, const SelectionMask& mask, RectI region);

// In-memory clipboard payload: the pixels under a selection plus the mask that
// shaped them, kept in region-local coordinates so the same block can be pasted
// anywhere on the canvas. (Crossing the OS clipboard is M4.)
struct ClipboardContent {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;  // width * height entries, 1 = selected
    PixelBuffer pixels;               // width * height

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
};

// The selection's contents while they float above the layer. Rotate, flip,
// move and resize only transform this copy, so the canvas underneath stays
// exactly as it was until the selection is committed (deselect, paste, another
// tool taking over, ...) — rotating 90° twice really is a 180° rotation of the
// untouched originals instead of two lifts that pile background color into the
// corners.
struct FloatingContents {
    PixelBuffer content;                    // pixels, laid out for `position`
    RectI position;                         // document rect `content` sits in
    std::vector<std::uint8_t> mask;         // position.width * position.height, 1 = selected
    RectI origin;                           // document rect the contents were lifted from
    std::vector<std::uint8_t> origin_mask;  // origin.width * origin.height, 1 = lifted
    Rgba8 fill;                             // background color shown at `origin`

    // A run of turns shares these: the box the run started from, and how far
    // it has turned since. Every turn squares the contents up with that box
    // first, so a run of turns lands where one turn of its total angle would.
    RectI turn_bounds;
    float rotation = 0.0f;  // degrees in [-180, 180), clockwise, about `turn_bounds`

    // The contents at their last full-resolution state: as lifted, or as left
    // by a rotate/flip. Rescaling samples from here rather than from
    // `content`, so squashing a selection and stretching it back restores the
    // original pixels instead of magnifying the already-thinned copy.
    PixelBuffer source;
    std::vector<std::uint8_t> source_mask;

    [[nodiscard]] bool empty() const noexcept { return content.width() <= 0; }

    friend bool operator==(const FloatingContents&, const FloatingContents&) = default;
};

// One reversible selection edit: the pixel region it touched, both ends of the
// selection mask, and — while the contents float — both ends of the floating
// copy, so undo/redo restores the selection along with the pixels.
struct SelectionEdit {
    RectI region;
    PixelBuffer before;
    PixelBuffer after;
    SelectionMask mask_before;
    SelectionMask mask_after;
    std::optional<FloatingContents> float_before;
    std::optional<FloatingContents> float_after;

    [[nodiscard]] bool empty() const noexcept {
        return region.empty() && mask_before == mask_after && float_before == float_after;
    }
};

// Wraps an angle into [-180, 180), so a full circle is no turn at all.
[[nodiscard]] float wrap_degrees(float degrees);

enum class ContentTransform { RotateCw90, RotateCcw90, Rotate180, FlipHorizontal, FlipVertical };

// Copies the masked pixels out of the layer. Empty when nothing is selected.
[[nodiscard]] ClipboardContent capture_clipboard(const PixelBuffer& pixels, const SelectionMask& mask);

// Copies the visible part of a floating selection. The layer does not hold the
// contents any more, so cut/copy read them from the float.
[[nodiscard]] ClipboardContent capture_floating(const FloatingContents& contents, RectI canvas);

// Clears the masked pixels and leaves the selection in place (Delete / Cut).
// The vacated pixels take `fill`, because mspaint leaves the background color
// behind wherever a selection used to be. Floating contents are discarded
// instead: only their origin is filled, everything they hovered over survives.
[[nodiscard]] SelectionEdit erase_contents(PixelBuffer& pixels, SelectionMask& mask,
                                           std::optional<FloatingContents>& floating, Rgba8 fill);

// Writes `clip` at (x, y) and makes the pasted block the new selection. The
// edit is empty when the block would land entirely off-canvas.
[[nodiscard]] SelectionEdit paste_clipboard(PixelBuffer& pixels, SelectionMask& mask,
                                            const ClipboardContent& clip, int x, int y);

// A floating copy of a block that did not come from the layer — a pasted image
// or a pasted selection. The contents hover above the canvas until the
// selection is committed, so nothing is painted over before then. `mask` holds
// width * height entries (1 = selected) laid out for the block; an empty one
// takes all of it. The origin stays empty, because a paste has nothing to
// restore behind it.
[[nodiscard]] FloatingContents float_block(PixelBuffer content, std::vector<std::uint8_t> mask, int x,
                                          int y);

// Lifts the masked pixels out of the layer into a floating copy. No-op when
// the selection is already floating, and the layer itself is never written to.
void lift_contents(const PixelBuffer& pixels, const SelectionMask& mask,
                   std::optional<FloatingContents>& floating, Rgba8 fill);

// Moves the floating contents (and the selection that describes them) by
// (dx, dy). Contents pushed off the canvas are kept, not dropped: the mask
// simply does not reach them until they are dragged back.
void translate_contents(SelectionMask& mask, FloatingContents& contents, int dx, int dy);

// Rotates or flips the floating contents, lifting them first when needed, and
// keeps the selection centred on the same place. The layer is not modified.
[[nodiscard]] SelectionEdit transform_contents(const PixelBuffer& pixels, SelectionMask& mask,
                                               std::optional<FloatingContents>& floating,
                                               ContentTransform transform, Rgba8 fill);

// Turns the floating contents by `degrees` about their centre, clockwise on
// screen, lifting them first when needed. The selection becomes the
// axis-aligned box the turned corners need, so anything the turn leaves
// outside the block ends up unselected. Turns add up, so a run of turns totalling
// a full circle restores the contents as they were. The layer is not modified.
[[nodiscard]] SelectionEdit rotate_contents(const PixelBuffer& pixels, SelectionMask& mask,
                                            std::optional<FloatingContents>& floating, float degrees,
                                            Rgba8 fill);

// Rescales the selection (and its floating contents) to `new_bounds`, always
// sampling the lifted originals — never the result of an earlier rescale, so
// a squash followed by a stretch-back restores the pixels it started with.
[[nodiscard]] SelectionEdit resize_selection(SelectionMask& mask,
                                             std::optional<FloatingContents>& floating,
                                             RectI new_bounds);

// Stamps the floating contents into the layer: the origin takes the background
// color, the contents land where they now are. Clears the float.
[[nodiscard]] SelectionEdit commit_contents(PixelBuffer& pixels, SelectionMask& mask,
                                            std::optional<FloatingContents>& floating);

// Paints the floating contents — and the background color left behind at their
// origin — over `region` of `target`. This is what makes the lift visible
// without ever having touched the layers.
void draw_floating(PixelBuffer& target, const FloatingContents& contents, RectI region);

}  // namespace lsp
