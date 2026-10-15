#include "history/history_stack.hpp"

#include "config.hpp"

namespace lsp {

void HistoryStack::push(std::unique_ptr<ICommand> command) {
    m_redo.clear();
    m_undo.push_back(std::move(command));
    if (m_undo.size() > kMaxHistoryEntries) m_undo.pop_front();
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
    m_modified = false;
}

}  // namespace lsp
