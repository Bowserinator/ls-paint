#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "brushes/brush.hpp"
#include "core/canvas_document.hpp"
#include "core/raster.hpp"
#include "history/commands/selection_edit_command.hpp"
#include "history/history_stack.hpp"
#include "tools/shape_tool.hpp"
#include "tools/text_tool.hpp"
#include "tools/tool.hpp"

namespace lsp {

struct ColorPair {
    Rgba8 primary{0, 0, 0};
    Rgba8 secondary = kWhite;

    [[nodiscard]] Rgba8& for_button(PointerButton button) noexcept {
        return button == PointerButton::Primary ? primary : secondary;
    }

    [[nodiscard]] const Rgba8& for_button(PointerButton button) const noexcept {
        return button == PointerButton::Primary ? primary : secondary;
    }

    // Tools read their ink through here: a color with alpha 0 paints nothing
    // while the stroke still runs, which looks like a dead pencil. for_button()
    // stays the raw write path for the dropper and the UI.
    [[nodiscard]] Rgba8 ink(PointerButton button) const noexcept {
        Rgba8 color = for_button(button);
        color.a = kChannelMax;
        return color;
    }

    // The other button's ink: a shape draws its border in the pressed button's
    // color and its interior in this one, mspaint's Color 1 / Color 2 split.
    [[nodiscard]] Rgba8 other_ink(PointerButton button) const noexcept {
        return ink(button == PointerButton::Primary ? PointerButton::Secondary
                                                    : PointerButton::Primary);
    }
};

// The path a selection gesture has drawn so far, so the view can outline it
// while it is still being drawn.
struct SelectionPreview {
    std::vector<Point2f> points;
};

// What the selection tool tells the UI about rotating: whether the pointer is
// in the zone just outside a corner, and whether a turn is under way.
struct RotatePreview {
    bool hover = false;
    bool rotating = false;
};

// The pointer shape that goes with a resize grip, by the line the grip drags
// along.
enum class ResizeCursor { None, Horizontal, Vertical, DiagonalNwSe, DiagonalNeSw };

// What the selection tool tells the UI about resizing: the shape the pointer
// takes over a grip, or while a resize drag runs.
struct ResizePreview {
    ResizeCursor cursor = ResizeCursor::None;
};

// What the active tool tells the UI about a gesture that is still running.
struct ToolUiState {
    ShapePreview shape;
    TextOverlayState text;
    SelectionPreview selection;
    RotatePreview rotate;
    ResizePreview resize;
};

// The active layer as a raster target that the selection holds back.
struct LayerTarget {
    const SelectionMask& clip;
    PixelBuffer& pixels;

    [[nodiscard]] raster::Target target() const { return {pixels, &clip}; }
};

// Everything a tool is allowed to reach: the document it edits, the history it
// records into, the brush/colors it paints with, and the selection shape the
// ribbon is currently showing.
struct ToolContext {
    CanvasDocument& document;
    HistoryStack& history;
    const Brush& brush;
    ColorPair& colors;
    const SelectionShape& shape;
    ShapeOptions& shape_options;
    TextOptions& text_options;
    ToolUiState& ui;

    // Shift held during the pointer event being handled.
    bool shift = false;

    // Screen pixels per document pixel of the view the pointer works in.
    float zoom = 1.0f;

    // Applies a floating selection: the contents are stamped into the layer and
    // the background color they were lifted with becomes real pixels. Anything
    // that makes the selection stop being "live" — deselect, a new marquee,
    // paste, another tool taking over — applies it first, like mspaint does.
    void commit_selection() {
        if (!document.floating()) return;
        const std::size_t layer_index = document.layers().active_index();
        SelectionEdit edit = commit_contents(document.active_layer().pixels, document.selection(),
                                             document.floating(), document.transparent_color());
        if (edit.empty()) return;
        document.mark_dirty(edit.region);
        history.push(make_selection_edit(layer_index, std::move(edit)));
    }

    [[nodiscard]] LayerTarget layer_target() const {
        return {document.selection(), document.active_layer().pixels};
    }
};

}  // namespace lsp
