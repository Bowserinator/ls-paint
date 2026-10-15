#pragma once

#include <deque>
#include <memory>

#include "history/command.hpp"

namespace lsp {

class HistoryStack {
public:
    explicit HistoryStack(CanvasDocument& document) : m_document(document) {}

    void push(std::unique_ptr<ICommand> command);
    void undo();
    void redo();

private:
    CanvasDocument& m_document;
    std::deque<std::unique_ptr<ICommand>> m_undo;
    std::deque<std::unique_ptr<ICommand>> m_redo;
};

}  // namespace lsp
