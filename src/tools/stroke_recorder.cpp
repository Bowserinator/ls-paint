#include "tools/stroke_recorder.hpp"

#include "history/commands/paint_stroke_command.hpp"

namespace lsp {

StrokeRecorder::StrokeRecorder(CanvasDocument& document)
    : m_document(document),
      m_layer_index(document.layers().active_index()),
      m_snapshot(document.active_layer().pixels) {}

void StrokeRecorder::touch(RectI region) {
    m_region = m_region.united(region.intersected(m_document.bounds()));
    m_document.mark_dirty(region);
}

void StrokeRecorder::discard() {
    if (m_region.empty()) return;
    const PixelBuffer before = m_snapshot.copy_region(m_region);
    m_document.layers().at(m_layer_index).pixels.blit(m_region.x, m_region.y, before);
    m_document.mark_dirty(m_region);
    m_region = {};
}

void StrokeRecorder::commit(HistoryStack& history) {
    if (m_region.empty()) return;
    const PixelBuffer& current = m_document.layers().at(m_layer_index).pixels;
    const PixelBuffer before = m_snapshot.copy_region(m_region);
    const PixelBuffer after = current.copy_region(m_region);
    // A stroke that never reached a pixel — clipped away by the selection, for
    // instance — must not take up an undo step that changes nothing.
    if (before == after) return;
    history.push(std::make_unique<PaintStrokeCommand>(m_layer_index, m_region, before, after));
}

}  // namespace lsp
