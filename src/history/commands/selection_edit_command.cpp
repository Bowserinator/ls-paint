#include "history/commands/selection_edit_command.hpp"

#include "core/canvas_document.hpp"

namespace lsp {

void SelectionEditCommand::undo(CanvasDocument& document) {
    m_pixels.undo(document.layers().at(m_layer_index).pixels);
    apply(document, m_mask_before, m_float_before);
}

void SelectionEditCommand::redo(CanvasDocument& document) {
    m_pixels.redo(document.layers().at(m_layer_index).pixels);
    apply(document, m_mask_after, m_float_after);
}

void SelectionEditCommand::apply(CanvasDocument& document, const SelectionMask& mask,
                                 const std::optional<FloatingContents>& floating) const {
    document.mark_dirty(m_region);
    // A float is drawn over the whole area it covers and follows the mask, so
    // either side appearing, going away or changing shape repaints the canvas.
    const bool touches_float = floating.has_value() || document.floating().has_value();
    document.selection() = mask;
    document.floating() = floating;
    if (touches_float) document.mark_dirty(document.bounds());
}

}  // namespace lsp
