#pragma once

#include "tools/brush_stroke_tool.hpp"
#include "tools/tool_context.hpp"

namespace lsp {

class PencilTool final : public BrushStrokeTool {
protected:
    Rgba8 stroke_color(ToolContext& context, PointerButton button) const override {
        return context.colors.ink(button);
    }
};

}  // namespace lsp
