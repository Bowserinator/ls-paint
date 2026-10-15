#pragma once

namespace lsp {

class CanvasDocument;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void undo(CanvasDocument& document) = 0;
    virtual void redo(CanvasDocument& document) = 0;
};

}  // namespace lsp
