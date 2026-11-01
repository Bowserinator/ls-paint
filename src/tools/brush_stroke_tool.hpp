#pragma once

#include <array>
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
    bool on_cancel(ToolContext& context) override;

protected:
    virtual Rgba8 stroke_color(ToolContext& context, PointerButton button) const = 0;

private:
    void draw_curve(ToolContext& context, Point2f next);

    std::array<Point2f, 3> m_points{};
    bool m_pending = false;
    std::optional<StrokeRecorder> m_recorder;
    std::optional<BrushEngine> m_engine;
};

}  // namespace lsp
