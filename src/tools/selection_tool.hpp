#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "selection/selection_ops.hpp"
#include "tools/tool.hpp"

namespace lsp {

// The eight resize grips of a selection: four corners plus the middle of every
// edge.
enum class SelectionHandle { Nw, N, Ne, E, Se, S, Sw, W };

// The grips in the order a press looks for them: corners first, so a small
// selection prefers them over the edge grips.
inline constexpr std::array kSelectionHandles{SelectionHandle::Nw, SelectionHandle::Ne,
                                              SelectionHandle::Se, SelectionHandle::Sw,
                                              SelectionHandle::N,  SelectionHandle::E,
                                              SelectionHandle::S,  SelectionHandle::W};

// Resize grips are only offered on selections at least this many pixels across,
// so the grips do not swallow the "drag inside to move it" target.
inline constexpr int kMinHandleSpan = 8;

// Where a grip sits: on the border pixels of `bounds`.
[[nodiscard]] Point2f handle_anchor(RectI bounds, SelectionHandle handle);

class SelectionTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;
    void on_key_pressed(ToolContext& context, std::string_view text) override;
    bool on_cancel(ToolContext& context) override;
    void on_deactivate(ToolContext& context) override;

private:
    enum class Drag { None, Marquee, Contents, Resize, Rotate };

    void finish_contents_drag(ToolContext& context);
    void finish_resize_drag(ToolContext& context, Point2f position);
    void rotate_to(ToolContext& context, Point2f position);
    void finish_rotate_drag(ToolContext& context, Point2f position);

    // Records the drag as one undo step from the snapshot to the current state.
    void push_drag_edit(ToolContext& context);

    Drag m_drag = Drag::None;
    Point2f m_marquee_anchor;
    Point2f m_contents_start;
    // Offset the contents drag has applied so far.
    int m_contents_dx = 0;
    int m_contents_dy = 0;
    // Lasso vertices, or the traced path in freehand mode.
    std::vector<Point2f> m_polygon;

    // The selection as it was when a contents/resize drag started, so undo
    // hands back exactly what the drag began with — including the floating
    // copy, which by then may have been translated, rotated or rescaled.
    SelectionMask m_mask_before;
    std::optional<FloatingContents> m_float_before;

    SelectionHandle m_handle = SelectionHandle::Nw;
    RectI m_resize_bounds;   // bounds the drag started from
    RectI m_resize_applied;  // bounds currently applied

    // A rotate drag pivots on the contents' centre when the press landed, and
    // measures the turn from the angle pressed, so the contents follow the
    // pointer instead of jumping to it.
    Point2f m_rotate_centre;
    float m_rotate_angle_start = 0.0f;
    // Clockwise turn the last frame applied, in [-180, 180).
    float m_rotate_degrees = 0.0f;
};

}  // namespace lsp
