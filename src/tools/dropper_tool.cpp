#include "tools/dropper_tool.hpp"

#include <cmath>

#include "tools/tool_context.hpp"

namespace lsp {

void DropperTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    const PixelBuffer& flattened = context.document.flattened();
    const int x = static_cast<int>(std::floor(position.x));
    const int y = static_cast<int>(std::floor(position.y));
    if (!flattened.contains(x, y)) return;
    Rgba8 picked = flattened.at(x, y);
    picked.a = 255;  // picking must not yield invisible ink
    context.colors.for_button(button) = picked;
}

}  // namespace lsp
