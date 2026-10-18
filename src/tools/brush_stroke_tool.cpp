#include "tools/brush_stroke_tool.hpp"

#include "tools/tool_context.hpp"

namespace lsp {

void BrushStrokeTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    if (m_recorder) return;
    m_recorder.emplace(context.document);
    m_engine.emplace(context.brush, stroke_color(context, button), context.document.selection());
    on_pointer_move(context, position);
}

void BrushStrokeTool::on_pointer_move(ToolContext& context, Point2f position) {
    if (!m_engine) return;
    m_recorder->touch(m_engine->stroke_to(context.document.active_layer().pixels, position));
}

void BrushStrokeTool::on_pointer_up(ToolContext& context, Point2f position) {
    if (!m_recorder) return;
    on_pointer_move(context, position);
    m_recorder->commit(context.history);
    m_recorder.reset();
    m_engine.reset();
}

bool BrushStrokeTool::on_cancel(ToolContext&) {
    if (!m_recorder) return false;
    m_recorder->discard();
    m_recorder.reset();
    m_engine.reset();
    return true;
}

}  // namespace lsp
