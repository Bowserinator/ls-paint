#pragma once

#include "app/input_router.hpp"
#include "brushes/brush.hpp"
#include "core/canvas_document.hpp"
#include "history/history_stack.hpp"
#include "selection/selection_ops.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool_context.hpp"
#include "tools/tool_registry.hpp"

namespace lsp {

class AppController {
public:
    AppController();

    [[nodiscard]] CanvasDocument& document() noexcept { return m_document; }
    [[nodiscard]] HistoryStack& history() noexcept { return m_history; }
    [[nodiscard]] InputRouter& input() noexcept { return m_router; }
    [[nodiscard]] Brush& brush() noexcept { return m_brush; }
    [[nodiscard]] ColorPair& colors() noexcept { return m_colors; }
    [[nodiscard]] ShapeOptions& shape_options() noexcept { return m_shape_options; }
    [[nodiscard]] TextOptions& text_options() noexcept { return m_text_options; }
    [[nodiscard]] ToolUiState& tool_ui() noexcept { return m_tool_ui; }
    [[nodiscard]] ToolContext& context() noexcept { return m_context; }
    [[nodiscard]] ITool* tool(ToolId id) const noexcept { return m_registry.find(id); }
    void set_selection_shape(SelectionShape shape) noexcept { m_selection_shape = shape; }

    void resize_canvas(int width, int height, Rgba8 fill);
    // Draws a committed text box into the active layer as one history entry.
    void commit_text(Point2f origin, std::string_view text, Rgba8 color);
    void crop_to_selection();
    void rotate(CanvasDocument::Rotation rotation);
    void flip_horizontal();
    void flip_vertical();

    void select_all();
    void invert_selection();
    void clear_selection();
    void make_selection_transparent();

    // Selection content clipboard (cut/copy/paste/delete and friends). All of
    // these act on the active layer inside the current selection.
    void copy_selection();
    void cut_selection();
    void delete_selection_contents();
    void paste_clipboard();
    void transform_selection_contents(ContentTransform transform);
    [[nodiscard]] const ClipboardContent& clipboard() const noexcept { return m_clipboard; }
    [[nodiscard]] bool has_selection() const noexcept { return m_document.selection().active(); }

private:
    void commit_selection_contents();
    void push_geometry_edit(std::vector<PixelBuffer> before, SelectionMask selection_before);
    void push_selection_edit(SelectionEdit edit);

    CanvasDocument m_document;
    HistoryStack m_history;
    Brush m_brush;
    ColorPair m_colors;
    ClipboardContent m_clipboard;
    SelectionShape m_selection_shape = SelectionShape::Rectangular;
    // Declared before m_context: it binds references to these.
    ShapeOptions m_shape_options;
    TextOptions m_text_options;
    ToolUiState m_tool_ui;
    ToolContext m_context;
    ToolRegistry m_registry;
    InputRouter m_router;
};

}  // namespace lsp
