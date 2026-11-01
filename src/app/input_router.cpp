#include "app/input_router.hpp"

#include "tools/tool_context.hpp"

namespace lsp {

void InputRouter::pointer_down(ToolId tool, Point2f position, PointerButton button) {
    if (m_active) {
        // A gesture owns the pointer until it ends. The other button asks for
        // it back: the gesture is abandoned rather than left to swallow the
        // press, which is what Escape does too.
        if (button == PointerButton::Secondary) cancel(tool);
        return;
    }
    // Any tool other than the selection tool applies a floating selection
    // first: the stroke about to start belongs to the layer underneath it.
    if (tool != ToolId::Selection) m_context.commit_selection();
    m_active = m_registry.find(tool);
    if (m_active) {
        m_press_button = button;
        m_active->on_pointer_down(m_context, position, button);
    }
}

void InputRouter::pointer_move(ToolId tool, Point2f position, bool shift) {
    m_context.shift = shift;
    ITool* instance = m_active != nullptr ? m_active : m_registry.find(tool);
    if (instance) instance->on_pointer_move(m_context, position);
}

void InputRouter::pointer_up(Point2f position, PointerButton button) {
    if (!m_active || button != m_press_button) return;
    m_active->on_pointer_up(m_context, position);
    m_active = nullptr;
}

void InputRouter::pointer_cancel() {
    // The pointer came back before the gesture could end, so there is no
    // release to look forward to: abandon what it has done instead.
    if (m_active) {
        (void)m_active->on_cancel(m_context);
        m_active = nullptr;
    }
}

void InputRouter::key(ToolId tool, std::string_view text) {
    // Mid-gesture keys belong to the gesture, not to the tool's idle handling.
    if (m_active) return;
    if (ITool* instance = m_registry.find(tool)) instance->on_key_pressed(m_context, text);
}

bool InputRouter::cancel(ToolId tool) {
    if (m_active) {
        if (!m_active->on_cancel(m_context)) return false;
        m_active = nullptr;
        return true;
    }
    // Nothing in flight: the selected tool may still have something to abandon,
    // such as a block hovering over the canvas.
    ITool* instance = m_registry.find(tool);
    return instance != nullptr && instance->on_cancel(m_context);
}

void InputRouter::double_click(ToolId tool) {
    if (ITool* instance = m_registry.find(tool)) instance->on_double_click(m_context);
}

void InputRouter::deactivate(ToolId tool) {
    // A tool switch ends any gesture that tool still owns; the tool itself does
    // the abandoning, so the router only has to forget it.
    if (m_active == m_registry.find(tool)) m_active = nullptr;
    if (ITool* instance = m_registry.find(tool)) instance->on_deactivate(m_context);
}

}  // namespace lsp
