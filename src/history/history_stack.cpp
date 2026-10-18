#include "history/history_stack.hpp"

#include "config.hpp"

namespace lsp {

void HistoryStack::push(std::unique_ptr<ICommand> command) {
    // A push below the save point drops the branch that held the saved state,
    // so undoing can no longer reach it.
    if (m_save_point && m_undo.size() < *m_save_point) m_save_point.reset();
    m_redo.clear();
    m_undo.push_back(std::move(command));
    trim();
    m_modified = true;
}

void HistoryStack::undo() {
    if (m_undo.empty()) return;
    m_undo.back()->undo(m_document);
    m_redo.push_back(std::move(m_undo.back()));
    m_undo.pop_back();
    m_modified = true;
}

void HistoryStack::redo() {
    if (m_redo.empty()) return;
    m_redo.back()->redo(m_document);
    m_undo.push_back(std::move(m_redo.back()));
    m_redo.pop_back();
    m_modified = true;
}

void HistoryStack::clear() {
    m_undo.clear();
    m_redo.clear();
    m_save_point.reset();
    m_modified = false;
}

std::size_t HistoryStack::held_bytes() const noexcept {
    std::size_t total = 0;
    for (const std::unique_ptr<ICommand>& entry : m_undo) total += entry->memory_bytes();
    for (const std::unique_ptr<ICommand>& entry : m_redo) total += entry->memory_bytes();
    return total;
}

void HistoryStack::trim() {
    // Redo entries furthest from the present go first: the nearest redo is the
    // one most likely to be wanted.
    while (held_bytes() > kMaxHistoryBytes && !m_redo.empty()) m_redo.pop_front();
    while (m_undo.size() > kMaxHistoryEntries ||
           (m_undo.size() > 1 && held_bytes() > kMaxHistoryBytes)) {
        m_undo.pop_front();
        // The saved state moves down with the entries in front of it, and is
        // gone once its own entry is the one dropped.
        if (m_save_point) {
            if (*m_save_point == 0) {
                m_save_point.reset();
            } else {
                --*m_save_point;
            }
        }
    }
}

}  // namespace lsp
