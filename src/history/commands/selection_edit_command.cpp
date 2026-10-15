#include "history/commands/selection_edit_command.hpp"

#include "core/canvas_document.hpp"

namespace lsp {

void SelectionEditCommand::apply(CanvasDocument& document, const PixelBuffer& pixels,
                                 const SelectionMask& mask,
                                 const std::optional<FloatingContents>& floating) const {
    if (!m_region.empty()) {
        document.layers().at(m_layer_index).pixels.blit(m_region.x, m_region.y, pixels);
        document.mark_dirty(m_region);
    }
    // A float is drawn over the whole area it covers and follows the mask, so
    // either side appearing, going away or changing shape repaints the canvas.
    const bool touches_float = floating.has_value() || document.floating().has_value();
    document.selection() = mask;
    document.floating() = floating;
    if (touches_float) document.mark_dirty(document.bounds());
}

}  // namespace lsp
