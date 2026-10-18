#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "core/canvas_document.hpp"
#include "history/history_stack.hpp"

namespace lsp {

class StrokeRecorder {
public:
    explicit StrokeRecorder(CanvasDocument& document);

    void touch(RectI region);
    void commit(HistoryStack& history);
    // The layer as the stroke found it: what an effect brush samples from.
    [[nodiscard]] const PixelBuffer& snapshot() const noexcept { return m_snapshot; }
    // Puts back what the stroke covered: a cancelled gesture leaves no trace.
    void discard();

private:
    // Calls `visit` with the area of every square the stroke has touched.
    template <typename VisitFn>
    void for_each_touched(VisitFn visit) const;

    CanvasDocument& m_document;
    std::size_t m_layer_index;
    PixelBuffer m_snapshot;
    std::unordered_set<std::uint64_t> m_touched;
};

}  // namespace lsp
