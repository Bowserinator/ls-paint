#pragma once

#include <cstddef>
#include <utility>

#include "core/pixel_buffer.hpp"
#include "history/command.hpp"

namespace lsp {

class PaintStrokeCommand final : public ICommand {
public:
    PaintStrokeCommand(std::size_t layer_index, RectI region, PixelBuffer before, PixelBuffer after)
        : m_layer_index(layer_index),
          m_region(region),
          m_before(std::move(before)),
          m_after(std::move(after)) {}

    void undo(CanvasDocument& document) override { apply(document, m_before); }
    void redo(CanvasDocument& document) override { apply(document, m_after); }

    [[nodiscard]] std::size_t memory_bytes() const noexcept override {
        return pixel_bytes(m_before) + pixel_bytes(m_after);
    }

private:
    void apply(CanvasDocument& document, const PixelBuffer& pixels) const;

    std::size_t m_layer_index;
    RectI m_region;
    PixelBuffer m_before;
    PixelBuffer m_after;
};

}  // namespace lsp
