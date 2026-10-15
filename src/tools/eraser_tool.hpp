#pragma once

#include "tools/brush_stroke_tool.hpp"
#include "tools/tool_context.hpp"

namespace lsp {

class EraserTool final : public BrushStrokeTool {
protected:
    Rgba8 stroke_color(ToolContext& context, PointerButton) const override {
        return context.colors.ink(PointerButton::Secondary);
    }
};

}  // namespace lsp
