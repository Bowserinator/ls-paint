#include "app/input_router.hpp"

#include "tools/tool_context.hpp"

namespace lsp {

void InputRouter::pointer_down(ToolId tool, Point2f position, PointerButton button) {
    if (m_active) return;
    // Any tool other than the selection tool applies a floating selection
    // first: the stroke about to start belongs to the layer underneath it.
    if (tool != ToolId::Selection) m_context.commit_selection();
    m_active = m_registry.find(tool);
    if (m_active) m_active->on_pointer_down(m_context, position, button);
}

void InputRouter::pointer_move(Point2f position) {
    if (m_active) m_active->on_pointer_move(m_context, position);
}

void InputRouter::pointer_up(Point2f position) {
    if (!m_active) return;
    m_active->on_pointer_up(m_context, position);
    m_active = nullptr;
}

}  // namespace lsp
