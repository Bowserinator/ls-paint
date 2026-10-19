#include "tools/stroke_recorder.hpp"

#include <algorithm>
#include <vector>

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
    std::vector<RectI> areas;
    areas.reserve(m_touched.size());
    for_each_touched([&](RectI area) { areas.push_back(area); });

    // A stroke that covered most of a band of rows is recorded as the band
    // itself, which is cheaper than the same pixels copied square by square.
    const int band_count = current.band_count();
    const int band_height = band_count > 0 ? current.band_rows(0).height : 1;
    std::vector<long long> covered(static_cast<std::size_t>(band_count), 0);
    for (const RectI& area : areas) {
        for (int band = area.y / band_height; band <= (area.bottom() - 1) / band_height; ++band) {
            const RectI rows = current.band_rows(band);
            const int overlap = std::min(area.bottom(), rows.bottom()) - std::max(area.y, rows.y);
            covered[static_cast<std::size_t>(band)] += static_cast<long long>(area.width) * overlap;
        }
    }
    std::vector<bool> whole(static_cast<std::size_t>(band_count), false);
    PixelDelta delta;
    for (int band = 0; band < band_count; ++band) {
        const RectI rows = current.band_rows(band);
        if (covered[static_cast<std::size_t>(band)] * 2 < static_cast<long long>(rows.width) * rows.height)
            continue;
        whole[static_cast<std::size_t>(band)] = true;
        delta.append(PixelDelta::between(m_snapshot, current, rows));
    }
    for (const RectI& area : areas) {
        bool inside_whole = true;
        for (int band = area.y / band_height; band <= (area.bottom() - 1) / band_height; ++band)
            inside_whole = inside_whole && whole[static_cast<std::size_t>(band)];
        if (!inside_whole) delta.append(PixelDelta::between(m_snapshot, current, area));
    }
    // A stroke that never reached a pixel — clipped away by the selection, for
    // instance — must not take up an undo step that changes nothing.
    if (delta.empty()) return;
    history.push(std::make_unique<PaintStrokeCommand>(m_layer_index, std::move(delta)));
}

}  // namespace lsp
