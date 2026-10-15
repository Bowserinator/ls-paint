#pragma once

#include <optional>

#include "brushes/brush_engine.hpp"
#include "tools/stroke_recorder.hpp"
#include "tools/tool.hpp"

namespace lsp {

class BrushStrokeTool : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;

protected:
    virtual Rgba8 stroke_color(ToolContext& context, PointerButton button) const = 0;

private:
    std::optional<StrokeRecorder> m_recorder;
    std::optional<BrushEngine> m_engine;
};

}  // namespace lsp
