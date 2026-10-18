#pragma once

#include <utility>
#include <vector>

#include "core/canvas_document.hpp"
#include "history/command.hpp"

namespace lsp {

// Undo entry for a whole-image geometry change (resize, crop, rotate, flip, or
// a paste that grew the canvas). It keeps the state the document had before
// the change — pixels, selection and hovering block together — and every undo
// or redo exchanges that with the document's, so no layer is ever copied and
// one state is held instead of two. The selection travels with it because
// CanvasDocument::restore() cannot know what the mask was: geometry changes
// always end deselected, and undo puts the selection back.
class CanvasResizeCommand final : public ICommand {
public:
    CanvasResizeCommand(std::vector<PixelBuffer> before, SelectionMask selection_before,
                        std::optional<FloatingContents> floating_before)
        : m_before(std::move(before)),
          m_selection(std::move(selection_before)),
          m_floating(std::move(floating_before)) {}

    void undo(CanvasDocument& document) override { exchange(document); }
    void redo(CanvasDocument& document) override { exchange(document); }

    [[nodiscard]] std::size_t memory_bytes() const noexcept override {
        std::size_t total = 0;
        for (const PixelBuffer& pixels : m_before) total += pixel_bytes(pixels);
        if (m_floating) total += pixel_bytes(m_floating->content);
        return total;
    }

private:
    void exchange(CanvasDocument& document) {
        document.swap_pixels(m_before);
        std::swap(m_selection, document.selection());
        std::swap(m_floating, document.floating());
    }

    std::vector<PixelBuffer> m_before;
    SelectionMask m_selection;
    std::optional<FloatingContents> m_floating;
};

}  // namespace lsp
