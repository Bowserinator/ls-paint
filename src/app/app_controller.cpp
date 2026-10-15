#include "app/app_controller.hpp"

#include "config.hpp"
#include "history/commands/canvas_resize_command.hpp"
#include "history/commands/paint_stroke_command.hpp"
#include "history/commands/selection_edit_command.hpp"
#include "selection/selection_ops.hpp"

namespace lsp {

AppController::AppController()
    : m_document(kDefaultCanvasWidth, kDefaultCanvasHeight),
      m_history(m_document),
      m_context{m_document, m_history, m_brush, m_colors, m_selection_shape},
      m_router(m_registry, m_context) {}

void AppController::resize_canvas(int width, int height, Rgba8 fill) {
    if (width == m_document.width() && height == m_document.height()) return;
    commit_selection_contents();
    auto before = m_document.snapshot();
    SelectionMask selection_before = m_document.selection();
    m_document.resize(width, height, fill);
    push_geometry_edit(std::move(before), std::move(selection_before));
}

void AppController::crop_to_selection() {
    const RectI region = m_document.selection().bounds();
    if (region.empty()) return;
    commit_selection_contents();
    auto before = m_document.snapshot();
    SelectionMask selection_before = m_document.selection();
    m_document.crop_to(region);
    push_geometry_edit(std::move(before), std::move(selection_before));
}

void AppController::rotate(CanvasDocument::Rotation rotation) {
    commit_selection_contents();
    auto before = m_document.snapshot();
    SelectionMask selection_before = m_document.selection();
    m_document.rotate(rotation);
    push_geometry_edit(std::move(before), std::move(selection_before));
}

void AppController::flip_horizontal() {
    commit_selection_contents();
    auto before = m_document.snapshot();
    SelectionMask selection_before = m_document.selection();
    m_document.flip_horizontal();
    push_geometry_edit(std::move(before), std::move(selection_before));
}

void AppController::flip_vertical() {
    commit_selection_contents();
    auto before = m_document.snapshot();
    SelectionMask selection_before = m_document.selection();
    m_document.flip_vertical();
    push_geometry_edit(std::move(before), std::move(selection_before));
}

// Whole-image geometry always ends deselected: the mask belongs to the old
// geometry, so only undo has anything to put back.
void AppController::push_geometry_edit(std::vector<PixelBuffer> before, SelectionMask selection_before) {
    m_history.push(std::make_unique<CanvasResizeCommand>(
        std::move(before), std::move(selection_before), m_document.snapshot(),
        SelectionMask(m_document.width(), m_document.height())));
}

void AppController::commit_selection_contents() { m_context.commit_selection(); }

void AppController::select_all() {
    commit_selection_contents();
    m_document.selection().fill_rect(m_document.bounds());
}

void AppController::invert_selection() {
    commit_selection_contents();
    m_document.selection().invert();
}

void AppController::clear_selection() {
    commit_selection_contents();
    m_document.selection().clear();
}

void AppController::make_selection_transparent() {
    commit_selection_contents();
    const SelectionMask& selection = m_document.selection();
    const RectI region = selection.active() ? selection.bounds() : m_document.bounds();
    if (region.empty()) return;

    const std::size_t layer_index = m_document.layers().active_index();
    PixelBuffer& pixels = m_document.active_layer().pixels;
    PixelBuffer before = pixels.copy_region(region);
    make_transparent(pixels, selection, region);
    m_document.mark_dirty(region);
    m_history.push(std::make_unique<PaintStrokeCommand>(layer_index, region, std::move(before), pixels.copy_region(region)));
}

void AppController::push_selection_edit(SelectionEdit edit) {
    if (edit.empty()) return;
    // A float-only edit never touches a pixel region, but taking the float
    // away (or moving it) repaints everything it covered.
    if (edit.float_before || edit.float_after)
        m_document.mark_dirty(m_document.bounds());
    else
        m_document.mark_dirty(edit.region);
    m_history.push(
        make_selection_edit(m_document.layers().active_index(), std::move(edit)));
}

void AppController::copy_selection() {
    if (m_document.floating()) {
        m_clipboard = capture_floating(*m_document.floating(), m_document.bounds());
        return;
    }
    m_clipboard = capture_clipboard(m_document.active_layer().pixels, m_document.selection());
}

void AppController::cut_selection() {
    copy_selection();
    delete_selection_contents();
}

void AppController::delete_selection_contents() {
    push_selection_edit(erase_contents(m_document.active_layer().pixels, m_document.selection(),
                                       m_document.floating(),
                                       m_colors.ink(PointerButton::Secondary)));
}

void AppController::paste_clipboard() {
    if (m_clipboard.empty()) return;
    commit_selection_contents();
    push_selection_edit(
        lsp::paste_clipboard(m_document.active_layer().pixels, m_document.selection(), m_clipboard, 0, 0));
}

void AppController::transform_selection_contents(ContentTransform transform) {
    push_selection_edit(transform_contents(m_document.active_layer().pixels, m_document.selection(),
                                           m_document.floating(), transform,
                                           m_colors.ink(PointerButton::Secondary)));
}

}  // namespace lsp
