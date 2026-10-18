#pragma once

#include <cstddef>

namespace lsp {

class CanvasDocument;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo(CanvasDocument& document) = 0;
    virtual void redo(CanvasDocument& document) = 0;

    // Roughly the bytes the entry holds, so the stack can keep a size budget
    // next to the entry count.
    [[nodiscard]] virtual std::size_t memory_bytes() const noexcept = 0;
};

}  // namespace lsp
