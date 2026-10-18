#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "core/pixel_buffer.hpp"
#include "history/command.hpp"
#include "selection/selection_mask.hpp"
#include "selection/selection_ops.hpp"

namespace lsp {

// Undo entry for an edit that changes both layer pixels and the selection
// (cut/copy/paste/delete, move/rotate/flip of a selection's contents). A plain
// PaintStrokeCommand would restore the pixels but leave the selection mask
// pointing at wherever it ended up after the edit.
//
// Edits made to a floating selection carry the floating copy itself instead of
// pixels: the layer never changed, so undo has to hand the contents back (or
// take them away) to show the difference.
class SelectionEditCommand final : public ICommand {
public:
    SelectionEditCommand(std::size_t layer_index, RectI region, PixelBuffer before, PixelBuffer after,
                         SelectionMask mask_before, SelectionMask mask_after,
                         std::optional<FloatingContents> float_before,
                         std::optional<FloatingContents> float_after)
        : m_layer_index(layer_index),
          m_region(region),
          m_before(std::move(before)),
          m_after(std::move(after)),
          m_mask_before(std::move(mask_before)),
          m_mask_after(std::move(mask_after)),
          m_float_before(std::move(float_before)),
          m_float_after(std::move(float_after)) {}

    void undo(CanvasDocument& document) override {
        apply(document, m_before, m_mask_before, m_float_before);
    }
    void redo(CanvasDocument& document) override {
        apply(document, m_after, m_mask_after, m_float_after);
    }

    [[nodiscard]] std::size_t memory_bytes() const noexcept override {
        std::size_t total = pixel_bytes(m_before) + pixel_bytes(m_after);
        if (m_float_before) total += pixel_bytes(m_float_before->content);
        if (m_float_after) total += pixel_bytes(m_float_after->content);
        return total;
    }

private:
    void apply(CanvasDocument& document, const PixelBuffer& pixels, const SelectionMask& mask,
               const std::optional<FloatingContents>& floating) const;

    std::size_t m_layer_index;
    RectI m_region;
    PixelBuffer m_before;
    PixelBuffer m_after;
    SelectionMask m_mask_before;
    SelectionMask m_mask_after;
    std::optional<FloatingContents> m_float_before;
    std::optional<FloatingContents> m_float_after;
};

[[nodiscard]] inline std::unique_ptr<SelectionEditCommand> make_selection_edit(
    std::size_t layer_index, SelectionEdit edit) {
    return std::make_unique<SelectionEditCommand>(
        layer_index, edit.region, std::move(edit.before), std::move(edit.after),
        std::move(edit.mask_before), std::move(edit.mask_after), std::move(edit.float_before),
        std::move(edit.float_after));
}

}  // namespace lsp
