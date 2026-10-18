#pragma once

#include <cstddef>

#include "core/canvas_document.hpp"
#include "history/history_stack.hpp"

namespace lsp {

class StrokeRecorder {
public:
    explicit StrokeRecorder(CanvasDocument& document);

    void touch(RectI region);
    void commit(HistoryStack& history);
    // Puts back what the stroke covered: a cancelled gesture leaves no trace.
    void discard();

private:
    CanvasDocument& m_document;
    std::size_t m_layer_index;
    PixelBuffer m_snapshot;
    RectI m_region;
};

}  // namespace lsp
