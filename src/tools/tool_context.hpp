#pragma once

#include "brushes/brush.hpp"
#include "core/canvas_document.hpp"
#include "history/commands/selection_edit_command.hpp"
#include "history/history_stack.hpp"
#include "tools/shape_tool.hpp"
#include "tools/text_tool.hpp"
#include "tools/tool.hpp"

namespace lsp {

struct ColorPair {
    Rgba8 primary{0, 0, 0};
    Rgba8 secondary{255, 255, 255};

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
        color.a = 255;
        return color;
    }
};

// What the active tool tells the UI about a gesture that is still running.
struct ToolUiState {
    ShapePreview shape;
    TextOverlayState text;
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

    // Applies a floating selection: the contents are stamped into the layer and
    // the background color they were lifted with becomes real pixels. Anything
    // that makes the selection stop being "live" — deselect, a new marquee,
    // paste, another tool taking over — applies it first, like mspaint does.
    void commit_selection() {
        if (!document.floating()) return;
        const std::size_t layer_index = document.layers().active_index();
        SelectionEdit edit =
            commit_contents(document.active_layer().pixels, document.selection(), document.floating());
        if (edit.empty()) return;
        document.mark_dirty(edit.region);
        history.push(make_selection_edit(layer_index, std::move(edit)));
    }

    // One byte per canvas pixel: 0 outside the current selection. Empty when
    // nothing is selected, in which case the whole canvas is writable.
    [[nodiscard]] std::vector<std::uint8_t> selection_clip() const {
        if (!document.selection().active()) return {};
        return document.selection().extract_region(document.bounds());
    }
};

}  // namespace lsp
