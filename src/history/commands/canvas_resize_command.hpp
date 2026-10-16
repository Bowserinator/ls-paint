#pragma once

#include <utility>
#include <vector>

#include "core/canvas_document.hpp"
#include "history/command.hpp"

namespace lsp {

// Undo entry for a whole-image geometry change (resize, crop, rotate, flip, or
// a paste that grew the canvas). It also carries the selection, because
// CanvasDocument::restore() cannot know what the mask was: geometry changes
// always end deselected, and undo puts the selection back.
class CanvasResizeCommand final : public ICommand {
public:
    CanvasResizeCommand(std::vector<PixelBuffer> before, SelectionMask selection_before,
                        std::vector<PixelBuffer> after, SelectionMask selection_after,
                        std::optional<FloatingContents> floating_after = std::nullopt)
        : m_before(std::move(before)),
          m_selection_before(std::move(selection_before)),
          m_after(std::move(after)),
          m_selection_after(std::move(selection_after)),
          m_floating_after(std::move(floating_after)) {}

    void undo(CanvasDocument& document) override {
        apply(document, m_before, m_selection_before, std::nullopt);
    }
    void redo(CanvasDocument& document) override {
        apply(document, m_after, m_selection_after, m_floating_after);
    }

private:
    static void apply(CanvasDocument& document, std::vector<PixelBuffer> pixels,
                      const SelectionMask& selection, std::optional<FloatingContents> floating) {
        document.restore(std::move(pixels));
        document.selection() = selection;
        document.floating() = std::move(floating);
    }

    std::vector<PixelBuffer> m_before;
    SelectionMask m_selection_before;
    std::vector<PixelBuffer> m_after;
    SelectionMask m_selection_after;
    // A paste leaves a block hovering on the canvas it grew, so the entry carries
    // the block back on redo.
    std::optional<FloatingContents> m_floating_after;
};

}  // namespace lsp
