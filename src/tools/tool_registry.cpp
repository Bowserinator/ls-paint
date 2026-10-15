#include "tools/tool_registry.hpp"

#include "tools/dropper_tool.hpp"
#include "tools/eraser_tool.hpp"
#include "tools/fill_tool.hpp"
#include "tools/pencil_tool.hpp"
#include "tools/selection_tool.hpp"

namespace lsp {

ToolRegistry::ToolRegistry() {
    m_tools.emplace(ToolId::Pencil, std::make_unique<PencilTool>());
    m_tools.emplace(ToolId::Eraser, std::make_unique<EraserTool>());
    m_tools.emplace(ToolId::Fill, std::make_unique<FillTool>());
    m_tools.emplace(ToolId::Dropper, std::make_unique<DropperTool>());
    m_tools.emplace(ToolId::Selection, std::make_unique<SelectionTool>());
}

ITool* ToolRegistry::find(ToolId id) const {
    const auto it = m_tools.find(id);
    return it == m_tools.end() ? nullptr : it->second.get();
}

}  // namespace lsp
