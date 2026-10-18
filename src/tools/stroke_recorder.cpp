#include "tools/stroke_recorder.hpp"

#include "config.hpp"
#include "history/commands/paint_stroke_command.hpp"

namespace lsp {

namespace {

constexpr std::uint64_t tile_key(int column, int row) noexcept {
    return (static_cast<std::uint64_t>(row) << 32) | static_cast<std::uint32_t>(column);
}

}  // namespace

StrokeRecorder::StrokeRecorder(CanvasDocument& document)
    : m_document(document),
      m_layer_index(document.layers().active_index()),
      m_snapshot(document.active_layer().pixels) {}

void StrokeRecorder::touch(RectI region) {
    m_document.mark_dirty(region);
    region = region.intersected(m_document.bounds());
    if (region.empty()) return;
    for (int row = region.y / kUndoTileSize; row <= (region.bottom() - 1) / kUndoTileSize; ++row)
        for (int column = region.x / kUndoTileSize; column <= (region.right() - 1) / kUndoTileSize; ++column)
            m_touched.insert(tile_key(column, row));
}

template <typename VisitFn>
void StrokeRecorder::for_each_touched(VisitFn visit) const {
    const RectI bounds = m_document.bounds();
    for (const std::uint64_t key : m_touched) {
        const int column = static_cast<int>(key & 0xffffffffu);
        const int row = static_cast<int>(key >> 32);
        visit(RectI{column * kUndoTileSize, row * kUndoTileSize, kUndoTileSize, kUndoTileSize}
                  .intersected(bounds));
    }
}

void StrokeRecorder::discard() {
    PixelBuffer& pixels = m_document.layers().at(m_layer_index).pixels;
    for_each_touched([&](RectI area) {
        pixels.copy_region_from(m_snapshot, area);
        m_document.mark_dirty(area);
    });
    m_touched.clear();
}

void StrokeRecorder::commit(HistoryStack& history) {
    const PixelBuffer& current = m_document.layers().at(m_layer_index).pixels;
    PixelDelta delta;
    for_each_touched([&](RectI area) { delta.append(PixelDelta::between(m_snapshot, current, area)); });
    // A stroke that never reached a pixel — clipped away by the selection, for
    // instance — must not take up an undo step that changes nothing.
    if (delta.empty()) return;
    history.push(std::make_unique<PaintStrokeCommand>(m_layer_index, std::move(delta)));
}

}  // namespace lsp
