#pragma once

#include <optional>
#include <vector>

#include "selection/selection_ops.hpp"
#include "tools/tool.hpp"

namespace lsp {

// The eight resize grips of a selection: four corners plus the middle of every
// edge.
enum class SelectionHandle { Nw, N, Ne, E, Se, S, Sw, W };

class SelectionTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;

private:
    enum class Drag { None, Marquee, Contents, Resize };

    void finish_contents_drag(ToolContext& context);
    void finish_resize_drag(ToolContext& context, Point2f position);

    Drag m_drag = Drag::None;
    Point2f m_anchor;
    Point2f m_contents_start;
    int m_dx = 0;
    int m_dy = 0;
    std::vector<Point2f> m_polygon;

    // The selection as it was when a contents/resize drag started, so undo
    // hands back exactly what the drag began with — including the floating
    // copy, which by then may have been translated, rotated or rescaled.
    SelectionMask m_mask_before;
    std::optional<FloatingContents> m_float_before;

    SelectionHandle m_handle = SelectionHandle::Nw;
    RectI m_resize_bounds;   // bounds the drag started from
    RectI m_resize_applied;  // bounds currently applied
};

}  // namespace lsp
