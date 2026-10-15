#include "history/commands/paint_stroke_command.hpp"

#include "core/canvas_document.hpp"

namespace lsp {

void PaintStrokeCommand::apply(CanvasDocument& document, const PixelBuffer& pixels) const {
    document.layers().at(m_layer_index).pixels.blit(m_region.x, m_region.y, pixels);
    document.mark_dirty(m_region);
}

}  // namespace lsp
