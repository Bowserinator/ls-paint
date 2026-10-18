#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app/input_router.hpp"
#include "brushes/brush.hpp"
#include "core/canvas_document.hpp"
#include "history/history_stack.hpp"
#include "io/document_io.hpp"
#include "selection/selection_ops.hpp"
#include "tools/tool_context.hpp"
#include "tools/tool_registry.hpp"

namespace lsp {

// What a file or clipboard action reports back to the UI. A failed action
// leaves the document untouched.
struct IoResult {
    bool ok = false;
    std::string error;

    [[nodiscard]] static IoResult success() noexcept { return {true, {}}; }
    [[nodiscard]] static IoResult failure(std::string message) { return {false, std::move(message)}; }
};

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

    // File. Each of these leaves the document untouched when it fails.
    IoResult new_document();
    IoResult open_document(const std::filesystem::path& path);
    IoResult save_document(const std::filesystem::path& path, ImageFormat format);
    IoResult save_document();
    [[nodiscard]] bool has_current_path() const noexcept { return !m_current_path.empty(); }
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept { return m_current_path; }
    [[nodiscard]] std::string display_name() const;
    [[nodiscard]] std::filesystem::path default_directory() const;

    // The system clipboard. Copy and cut also feed the selection clipboard, so
    // the pixels can leave the application and come back.
    IoResult copy_to_clipboard();
    IoResult paste_from_clipboard();
    IoResult paste_from_file(const std::filesystem::path& path);
    [[nodiscard]] bool selection_clipboard_empty() const noexcept { return m_clipboard.empty(); }

private:
    void commit_selection_contents();
    void install_floating(FloatingContents contents);
    void push_geometry_edit(std::vector<PixelBuffer> before, SelectionMask selection_before,
                            std::optional<FloatingContents> floating_after = std::nullopt);
    void push_selection_edit(SelectionEdit edit);
    IoResult paste_pixels(PixelBuffer image);
    // The region Copy and Cut take: the selection when there is one, otherwise
    // the whole canvas.
    [[nodiscard]] RectI copy_source_rect() const;

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
    std::filesystem::path m_current_path;
};

}  // namespace lsp
