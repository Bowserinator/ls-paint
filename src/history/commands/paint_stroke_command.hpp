#pragma once

#include <cstddef>
#include <utility>

#include "core/pixel_delta.hpp"
#include "history/command.hpp"

namespace lsp {

class PaintStrokeCommand final : public ICommand {
public:
    PaintStrokeCommand(std::size_t layer_index, PixelDelta delta)
        : m_layer_index(layer_index), m_delta(std::move(delta)) {}

    void undo(CanvasDocument& document) override;
    void redo(CanvasDocument& document) override;

    [[nodiscard]] std::size_t memory_bytes() const noexcept override { return m_delta.memory_bytes(); }

private:
    std::size_t m_layer_index;
    PixelDelta m_delta;
};

}  // namespace lsp
