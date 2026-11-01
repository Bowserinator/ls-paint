#include "tools/brush_stroke_tool.hpp"

#include <cmath>
#include <utility>

#include "tools/tool_context.hpp"

namespace lsp {

void BrushStrokeTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    if (m_recorder) return;
    m_recorder.emplace(context.document);
    m_engine.emplace(context.brush, stroke_color(context, button), context.document.selection(),
                     &m_recorder->snapshot());
    m_points = {position, position, position};
    m_pending = false;
    m_recorder->touch(m_engine->stroke_to(context.document.active_layer().pixels, position));
}

void BrushStrokeTool::on_pointer_move(ToolContext& context, Point2f position) {
    if (!m_engine || distance(m_points[2], position) < 1e-4f) return;
    if (m_pending) {
        draw_curve(context, position);
        m_points = {m_points[1], m_points[2], position};
    } else {
        const Point2f start = m_points[1];
        m_points[0] = {2 * start.x - position.x, 2 * start.y - position.y};
        m_points[2] = position;
        m_pending = true;
    }
}

void BrushStrokeTool::draw_curve(ToolContext& context, Point2f next) {
    const auto [p0, p1, p2] = m_points;
    const float t0 = std::sqrt(distance(p0, p1));
    const float t1 = std::sqrt(distance(p1, p2));
    const float t2 = std::sqrt(distance(p2, next));
    // Centripetal Catmull–Rom avoids loops when input samples are unevenly spaced.
    const auto controls = [&](float a, float b, float c, float d) {
        const float start = t1 * ((b - a) / t0 - (c - a) / (t0 + t1)) + c - b;
        const float end = t1 * ((d - c) / t2 - (d - b) / (t1 + t2)) + c - b;
        return std::pair{b + start / 3, c - end / 3};
    };
    const auto [x1, x2] = controls(p0.x, p1.x, p2.x, next.x);
    const auto [y1, y2] = controls(p0.y, p1.y, p2.y, next.y);
    RectI dirty;
    PixelBuffer& target = context.document.active_layer().pixels;
    const auto flatten = [&](auto&& self, Point2f a, Point2f b, Point2f c, Point2f d, int depth) -> void {
        const Point2f one_third{(2 * a.x + d.x) / 3, (2 * a.y + d.y) / 3};
        const Point2f two_thirds{(a.x + 2 * d.x) / 3, (a.y + 2 * d.y) / 3};
        if (depth == 10 || (distance(b, one_third) <= 0.25f && distance(c, two_thirds) <= 0.25f)) {
            dirty = dirty.united(m_engine->stroke_to(target, d));
            return;
        }
        const Point2f ab = midpoint(a, b), bc = midpoint(b, c), cd = midpoint(c, d);
        const Point2f abc = midpoint(ab, bc), bcd = midpoint(bc, cd);
        const Point2f middle = midpoint(abc, bcd);
        self(self, a, ab, abc, middle, depth + 1);
        self(self, middle, bcd, cd, d, depth + 1);
    };
    flatten(flatten, p1, {x1, y1}, {x2, y2}, p2, 0);
    m_recorder->touch(dirty);
}

void BrushStrokeTool::on_pointer_up(ToolContext& context, Point2f position) {
    if (!m_recorder) return;
    on_pointer_move(context, position);
    if (m_pending) {
        const Point2f from = m_points[1], to = m_points[2];
        draw_curve(context, {2 * to.x - from.x, 2 * to.y - from.y});
    }
    m_recorder->touch(m_engine->finish(context.document.active_layer().pixels));
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
