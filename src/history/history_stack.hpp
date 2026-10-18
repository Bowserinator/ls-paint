#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>

#include "history/command.hpp"

namespace lsp {

class HistoryStack {
public:
    explicit HistoryStack(CanvasDocument& document) : m_document(document) {}

    void push(std::unique_ptr<ICommand> command);
    void undo();
    void redo();

    // True when the document differs from what was last saved. The depth is
    // compared with the depth at the save, so undoing back to that state
    // reports the document as saved again.
    [[nodiscard]] bool is_modified() const noexcept {
        return m_save_point ? m_undo.size() != *m_save_point : m_modified;
    }
    // Remembers where in the stack the state on disk sits.
    void mark_clean() noexcept {
        m_modified = false;
        m_save_point = m_undo.size();
    }
    void clear();

private:
    // Bytes held by the entries of both stacks, for the size budget.
    [[nodiscard]] std::size_t held_bytes() const noexcept;
    // Drops entries until the stack fits the count and the size budget. The
    // newest undo entry is always kept, however large it is.
    void trim();

    CanvasDocument& m_document;
    std::deque<std::unique_ptr<ICommand>> m_undo;
    std::deque<std::unique_ptr<ICommand>> m_redo;
    // Where in m_undo the state on disk sits, while undoing can still reach it.
    std::optional<std::size_t> m_save_point;
    bool m_modified = false;
};

}  // namespace lsp
