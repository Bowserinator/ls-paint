#pragma once

#include <string_view>

#include "tools/tool.hpp"
#include "tools/tool_registry.hpp"

namespace lsp {

class InputRouter {
public:
    InputRouter(const ToolRegistry& registry, ToolContext& context) : m_registry(registry), m_context(context) {}

    void pointer_down(ToolId tool, Point2f position, PointerButton button);
    // The hover position still reaches the tool between clicks: a shape preview
    // and an open polygon both follow the cursor.
    void pointer_move(ToolId tool, Point2f position);
    void pointer_up(Point2f position);
    void key(ToolId tool, std::string_view text);
    void double_click(ToolId tool);
    void deactivate(ToolId tool);

    // True while a gesture is in flight. Shortcuts must not fire mid-stroke:
    // undoing the stroke that is still being recorded corrupts it.
    [[nodiscard]] bool active() const noexcept { return m_active != nullptr; }

private:
    const ToolRegistry& m_registry;
    ToolContext& m_context;
    ITool* m_active = nullptr;
};

}  // namespace lsp
