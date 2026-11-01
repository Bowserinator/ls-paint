#pragma once

#include "tools/tool.hpp"

namespace lsp {

class FillTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
};

}  // namespace lsp
