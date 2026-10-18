#include "history/commands/paint_stroke_command.hpp"

#include "core/canvas_document.hpp"

namespace lsp {

void PaintStrokeCommand::undo(CanvasDocument& document) {
    m_delta.undo(document.layers().at(m_layer_index).pixels);
    document.mark_dirty(m_delta.bounds());
}

void PaintStrokeCommand::redo(CanvasDocument& document) {
    m_delta.redo(document.layers().at(m_layer_index).pixels);
    document.mark_dirty(m_delta.bounds());
}

}  // namespace lsp
