#pragma once

#include <filesystem>
#include <optional>
#include <span>
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
    // Text outside `box`, when given, is cut off. Characters that `styles` has no
    // entry for are drawn in `color` with the text options.
    void commit_text(Point2f origin, std::string_view text, Rgba8 color, std::optional<RectI> box = std::nullopt,
                     std::span<const TextStyle> styles = {});
    void crop_to_selection();
    void rotate(CanvasDocument::Rotation rotation);
    void flip_horizontal();
    void flip_vertical();
    // Stretches the selection's contents to `width` x `height` and then shears
    // them, or does the same to the whole image when nothing is selected. The
    // two are one undo step. `fill` shows where a shear leaves the image bare.
    void resize_and_skew(int width, int height, float horizontal_degrees, float vertical_degrees,
                         Rgba8 fill);

    void select_all();
    void invert_selection();
    void clear_selection();
    // With this on, the background color does not paint when selection contents
    // are placed or copied: what is under them shows through instead.
    void set_transparent_selection(bool on);
    [[nodiscard]] bool transparent_selection() const noexcept { return m_transparent_selection; }
    // Re-reads the background color the transparent selection leaves out.
    void refresh_transparent_color();

    // Selection content clipboard (cut/copy/paste/delete and friends). All of
    // these act on the active layer inside the current selection.
    void copy_selection();
    void cut_selection();
    void delete_selection_contents();
    // Inverts the colors inside the selection, or of the whole layer when
    // nothing is selected.
    void invert_colors();
    void paste_clipboard();
    void transform_selection_contents(ContentTransform transform);
    [[nodiscard]] const ClipboardContent& clipboard() const noexcept { return m_clipboard; }
    // Whether `image` is what the last copy put on the system clipboard.
    [[nodiscard]] bool is_own_copy(const PixelBuffer& image) const;
    // Drops a block that is still hovering: the paste or lift never landed, so
    // nothing is written to the layer and no undo step is recorded.
    bool discard_floating();

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
    IoResult paste_from_file(const std::filesystem::path& path);
    // Pastes pixels the caller has already read, such as one off the system
    // clipboard.
    IoResult paste_image(PixelBuffer image);

private:
    void commit_selection_contents();
    void install_floating(FloatingContents contents);
    void push_geometry_edit(std::vector<PixelBuffer> before, SelectionMask selection_before);
    void push_selection_edit(SelectionEdit edit);
    // Applies `change` to the whole document as one undo step, after the
    // floating selection has been put down.
    template <typename ChangeFn>
    void edit_geometry(ChangeFn change) {
        commit_selection_contents();
        std::vector<PixelBuffer> before = m_document.snapshot();
        SelectionMask selection_before = m_document.selection();
        change();
        push_geometry_edit(std::move(before), std::move(selection_before));
    }
    // The selection's bounds, or the whole canvas when nothing is selected.
    [[nodiscard]] RectI copy_source_rect() const;

    CanvasDocument m_document;
    HistoryStack m_history;
    Brush m_brush;
    ColorPair m_colors;
    ClipboardContent m_clipboard;
    PixelBuffer m_system_copy;
    bool m_transparent_selection = false;
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
