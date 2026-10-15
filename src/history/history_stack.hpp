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

    // True once anything was pushed, undone or redone since the last mark_clean.
    // Undo counts: returning to the saved state still reports modified, which is
    // the safe direction for an unsaved-changes prompt.
    [[nodiscard]] bool is_modified() const noexcept { return m_modified; }
    void mark_clean() noexcept { m_modified = false; }
    void clear();

private:
    CanvasDocument& m_document;
    std::deque<std::unique_ptr<ICommand>> m_undo;
    std::deque<std::unique_ptr<ICommand>> m_redo;
    bool m_modified = false;
};

}  // namespace lsp
