#include "tools/selection_tool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

#include "history/commands/selection_edit_command.hpp"
#include "tools/tool_context.hpp"

namespace lsp {

namespace {

// A click this close to the polygon's first vertex closes the lasso.
constexpr float kClosePolygonDistance = 6.0f;

// Resize grips are only offered on selections big enough that the grips would
// not swallow the "drag inside to move it" target.
constexpr float kHandleHitRadius = 3.5f;
constexpr int kMinHandleSpan = 8;

// How far from a corner a press still starts a turn, the steps Shift snaps a
// turn to, and the turn below which a drag is treated as a click.
constexpr float kRotateHitRadius = 24.0f;
constexpr float kRotateStepDegrees = 45.0f;
constexpr float kMinRotateDegrees = 1.0f;

RectI rect_from_corners(Point2f a, Point2f b) {
    const int left = static_cast<int>(std::floor(std::min(a.x, b.x)));
    const int top = static_cast<int>(std::floor(std::min(a.y, b.y)));
    const int right = static_cast<int>(std::ceil(std::max(a.x, b.x)));
    const int bottom = static_cast<int>(std::ceil(std::max(a.y, b.y)));
    return {left, top, right - left, bottom - top};
}

bool contains_point(RectI rect, Point2f point) {
    return point.x >= static_cast<float>(rect.x) && point.y >= static_cast<float>(rect.y) &&
           point.x < static_cast<float>(rect.right()) && point.y < static_cast<float>(rect.bottom());
}

// The grips sit on the border pixels of the selection, corners first so a small
// selection prefers them over the edge grips.
Point2f handle_anchor(RectI bounds, SelectionHandle handle) {
    const float left = static_cast<float>(bounds.x);
    const float top = static_cast<float>(bounds.y);
    const float right = static_cast<float>(bounds.x + bounds.width - 1);
    const float bottom = static_cast<float>(bounds.y + bounds.height - 1);
    const float mid_x = left + (right - left) / 2.0f;
    const float mid_y = top + (bottom - top) / 2.0f;
    switch (handle) {
        case SelectionHandle::Nw: return {left, top};
        case SelectionHandle::N: return {mid_x, top};
        case SelectionHandle::Ne: return {right, top};
        case SelectionHandle::E: return {right, mid_y};
        case SelectionHandle::Se: return {right, bottom};
        case SelectionHandle::S: return {mid_x, bottom};
        case SelectionHandle::Sw: return {left, bottom};
        case SelectionHandle::W: return {left, mid_y};
    }
    return {left, top};
}

std::optional<SelectionHandle> handle_at(RectI bounds, Point2f position) {
    if (bounds.width < kMinHandleSpan || bounds.height < kMinHandleSpan) return std::nullopt;
    constexpr std::array kHandles{SelectionHandle::Nw, SelectionHandle::Ne, SelectionHandle::Se,
                                  SelectionHandle::Sw, SelectionHandle::N,  SelectionHandle::E,
                                  SelectionHandle::S,  SelectionHandle::W};
    for (const SelectionHandle handle : kHandles)
        if (distance(handle_anchor(bounds, handle), position) <= kHandleHitRadius) return handle;
    return std::nullopt;
}

// True when a press at `position` would start a turn: near a corner, but not on
// a resize grip or inside the selection, which take the press first.
bool in_rotate_zone(RectI bounds, Point2f position) {
    if (handle_at(bounds, position) || contains_point(bounds, position)) return false;
    constexpr std::array kCorners{SelectionHandle::Nw, SelectionHandle::Ne, SelectionHandle::Se,
                                  SelectionHandle::Sw};
    for (const SelectionHandle corner : kCorners)
        if (distance(handle_anchor(bounds, corner), position) <= kRotateHitRadius) return true;
    return false;
}

// Records whether a press at the pointer would start a turn, so the UI can
// show it.
void update_rotate_hover(ToolContext& context, Point2f position) {
    const SelectionMask& selection = context.document.selection();
    const RectI bounds = selection.bounds();
    context.ui.rotate.hover =
        selection.active() && !bounds.empty() && in_rotate_zone(bounds, position);
}

Point2f rect_centre(RectI bounds) {
    return {static_cast<float>(bounds.x) + bounds.width / 2.0f,
            static_cast<float>(bounds.y) + bounds.height / 2.0f};
}

// The point a turn pivots on: the centre of the run of turns the contents are
// already in, otherwise the centre of the selection.
Point2f rotate_pivot(RectI bounds, const std::optional<FloatingContents>& floating) {
    if (floating && floating->rotation != 0.0f) return rect_centre(floating->turn_bounds);
    return rect_centre(bounds);
}

// The pointer's bearing from a point, in degrees. Canvas y runs down, so a
// growing bearing is a turn to the right, which is the sense the contents turn
// in as well.
float bearing_degrees(Point2f centre, Point2f position) {
    const float radians = std::atan2(position.y - centre.y, position.x - centre.x);
    return radians * 180.0f / std::numbers::pi_v<float>;
}

// The rectangle the drag would produce: the edges opposite the grip stay put,
// and the result always stays on the canvas at least one pixel wide.
RectI resized_bounds(RectI start, SelectionHandle handle, Point2f position, RectI canvas) {
    int left = start.x;
    int top = start.y;
    int right = start.x + start.width;
    int bottom = start.y + start.height;

    const int px = static_cast<int>(std::lround(position.x));
    const int py = static_cast<int>(std::lround(position.y));

    const bool west = handle == SelectionHandle::Nw || handle == SelectionHandle::Sw ||
                      handle == SelectionHandle::W;
    const bool east = handle == SelectionHandle::Ne || handle == SelectionHandle::Se ||
                      handle == SelectionHandle::E;
    const bool north = handle == SelectionHandle::Nw || handle == SelectionHandle::Ne ||
                       handle == SelectionHandle::N;
    const bool south = handle == SelectionHandle::Sw || handle == SelectionHandle::Se ||
                       handle == SelectionHandle::S;

    if (west) left = std::clamp(px, canvas.x, canvas.right() - 1);
    if (east) right = std::clamp(px + 1, canvas.x + 1, canvas.right());
    if (north) top = std::clamp(py, canvas.y, canvas.bottom() - 1);
    if (south) bottom = std::clamp(py + 1, canvas.y + 1, canvas.bottom());

    // Dragging an edge past its opposite parks it one pixel away instead of
    // collapsing or flipping the selection.
    if (west && left >= right) left = right - 1;
    if (east && right <= left) right = left + 1;
    if (north && top >= bottom) top = bottom - 1;
    if (south && bottom <= top) bottom = top + 1;

    return {left, top, right - left, bottom - top};
}

}  // namespace

void SelectionTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    // The badge belongs to a free pointer over the rotate zone: any press takes
    // it away, and a press that starts a turn raises it again.
    context.ui.rotate.hover = false;
    context.ui.rotate.rotating = false;
    if (button == PointerButton::Secondary) return;  // reserved for the context menu

    const SelectionShape shape = context.shape;
    SelectionMask& selection = context.document.selection();

    if (shape == SelectionShape::Polygon && !m_polygon.empty()) {
        if (m_polygon.size() >= 3 && distance(position, m_polygon.front()) <= kClosePolygonDistance) {
            context.commit_selection();
            selection.fill_polygon(m_polygon);
            m_polygon.clear();
        } else {
            m_polygon.push_back(position);
        }
        context.ui.selection.points = m_polygon;
        return;
    }

    const RectI bounds = selection.bounds();
    if (selection.active() && !bounds.empty()) {
        // The grips sit on the border, which is also inside the selection, so
        // they win over "drag the contents around".
        if (const std::optional<SelectionHandle> handle = handle_at(bounds, position)) {
            m_drag = Drag::Resize;
            m_handle = *handle;
            m_resize_bounds = bounds;
            m_resize_applied = bounds;
            m_mask_before = selection;
            m_float_before = context.document.floating();
            return;
        }

        // Dragging inside an active selection moves its contents; anywhere else
        // starts a new marquee/lasso that replaces it.
        if (contains_point(bounds, position)) {
            m_contents_start = position;
            m_contents_dx = m_contents_dy = 0;
            m_mask_before = selection;
            m_float_before = context.document.floating();
            m_drag = Drag::Contents;
            return;
        }

        // Just outside a corner the press starts a turn instead of a new
        // marquee.
        if (in_rotate_zone(bounds, position)) {
            m_mask_before = selection;
            m_float_before = context.document.floating();
            m_rotate_centre = rotate_pivot(bounds, m_float_before);
            m_rotate_angle_start = bearing_degrees(m_rotate_centre, position);
            m_rotate_degrees = 0.0f;
            context.ui.rotate.hover = true;
            context.ui.rotate.rotating = true;
            m_drag = Drag::Rotate;
            return;
        }
    }

    // A new selection replaces the old one, which applies a floating one first.
    context.commit_selection();

    if (shape == SelectionShape::Polygon) {
        m_polygon.push_back(position);
        context.ui.selection.points = m_polygon;
        return;
    }

    m_marquee_anchor = position;
    m_drag = Drag::Marquee;
    m_polygon.clear();
    if (shape == SelectionShape::Freehand) m_polygon = {position};
    context.ui.selection.points = m_polygon;
}

void SelectionTool::on_pointer_move(ToolContext& context, Point2f position) {
    SelectionMask& selection = context.document.selection();

    // A drag in flight owns the badge, and so does a lasso being laid out; a
    // free pointer over the rotate zone raises it.
    if (m_drag == Drag::None && m_polygon.empty())
        update_rotate_hover(context, position);
    else
        context.ui.rotate.hover = false;

    if (m_drag == Drag::Contents) {
        const int dx = static_cast<int>(std::lround(position.x - m_contents_start.x));
        const int dy = static_cast<int>(std::lround(position.y - m_contents_start.y));
        if (dx == m_contents_dx && dy == m_contents_dy) return;

        // The lift only happens once the contents actually move: a press that
        // never travels leaves the layer — and the history — alone.
        std::optional<FloatingContents>& floating = context.document.floating();
        if (!floating)
            lift_contents(context.document.active_layer().pixels, selection, floating,
                          context.colors.ink(PointerButton::Secondary));
        if (floating) translate_contents(selection, *floating, dx - m_contents_dx, dy - m_contents_dy);

        m_contents_dx = dx;
        m_contents_dy = dy;
        context.document.mark_dirty(context.document.bounds());
        return;
    }

    if (m_drag == Drag::Resize) {
        const RectI next = resized_bounds(m_resize_bounds, m_handle, position, context.document.bounds());
        if (next == m_resize_applied) return;
        m_resize_applied = next;
        // Rebuild from the drag's snapshot, and let the resize resample the
        // lifted originals: no frame ever scales up what the last one thinned.
        selection = m_mask_before;
        context.document.floating() = m_float_before;
        (void)resize_selection(selection, context.document.floating(), next);
        context.document.mark_dirty(context.document.bounds());
        return;
    }

    if (m_drag == Drag::Rotate) {
        rotate_to(context, position);
        return;
    }

    // The rubber band runs from the last click to the pointer, so the outline
    // shows the edge the next click would place.
    if (m_drag == Drag::None && context.shape == SelectionShape::Polygon && !m_polygon.empty()) {
        context.ui.selection.points = m_polygon;
        context.ui.selection.points.push_back(position);
        return;
    }

    if (m_drag != Drag::Marquee) return;
    if (context.shape == SelectionShape::Rectangular)
        context.document.selection().fill_rect(rect_from_corners(m_marquee_anchor, position));
    else
        m_polygon.push_back(position);
    context.ui.selection.points = m_polygon;
}

void SelectionTool::on_pointer_up(ToolContext& context, Point2f position) {
    if (m_drag == Drag::Contents) {
        finish_contents_drag(context);
        return;
    }
    if (m_drag == Drag::Resize) {
        finish_resize_drag(context, position);
        return;
    }
    if (m_drag == Drag::Rotate) {
        finish_rotate_drag(context, position);
        return;
    }
    if (m_drag != Drag::Marquee) return;

    if (context.shape == SelectionShape::Rectangular)
        context.document.selection().fill_rect(rect_from_corners(m_marquee_anchor, position));
    else
        context.document.selection().fill_polygon(m_polygon);

    m_polygon.clear();
    m_drag = Drag::None;
    context.ui.selection.points.clear();
}

void SelectionTool::on_key_pressed(ToolContext& context, std::string_view text) {
    // Escape abandons a marquee that is still being drawn, and the view's copy
    // of the path goes with it.
    if (text != "\x1b") return;
    if (m_polygon.empty() && m_drag == Drag::None) return;
    m_polygon.clear();
    m_drag = Drag::None;
    context.ui.selection.points.clear();
}

void SelectionTool::finish_contents_drag(ToolContext& context) {
    std::optional<FloatingContents>& floating = context.document.floating();

    // A drag that ends where it began never happened: put the contents back
    // rather than leaving a lift that shows nothing new.
    if (m_contents_dx == 0 && m_contents_dy == 0 && !m_float_before && floating) floating.reset();

    context.document.mark_dirty(context.document.bounds());
    push_drag_edit(context);
    m_drag = Drag::None;
}

void SelectionTool::finish_resize_drag(ToolContext& context, Point2f position) {
    SelectionMask& selection = context.document.selection();
    std::optional<FloatingContents>& floating = context.document.floating();

    const RectI next = resized_bounds(m_resize_bounds, m_handle, position, context.document.bounds());
    // Always rebuild from the snapshot: the last frame may be somewhere else
    // entirely, and a drag that came back to the start has to end up pristine.
    selection = m_mask_before;
    floating = m_float_before;
    if (next != m_resize_bounds) (void)resize_selection(selection, floating, next);

    // Resizing a selection that never left the layer is geometry, like drawing
    // the marquee in the first place — only a floating copy carries its own
    // undo entry, because it is the only thing that changed.
    if (m_float_before) push_drag_edit(context);

    context.document.mark_dirty(context.document.bounds());
    m_drag = Drag::None;
}

void SelectionTool::rotate_to(ToolContext& context, Point2f position) {
    float degrees = wrap_degrees(bearing_degrees(m_rotate_centre, position) - m_rotate_angle_start);
    if (context.shift) degrees = wrap_degrees(std::round(degrees / kRotateStepDegrees) * kRotateStepDegrees);
    if (degrees == m_rotate_degrees) return;
    m_rotate_degrees = degrees;

    // Rebuild from the drag's snapshot, the way the resize drag does, so every
    // frame turns the pixels the drag began with instead of the last frame's
    // sampling. A drag that has not turned yet leaves the snapshot alone, which
    // keeps a press in the zone from lifting the contents.
    SelectionMask& selection = context.document.selection();
    std::optional<FloatingContents>& floating = context.document.floating();
    selection = m_mask_before;
    floating = m_float_before;
    context.document.mark_dirty(context.document.bounds());
    if (std::abs(degrees) < kMinRotateDegrees) return;

    (void)rotate_contents(context.document.active_layer().pixels, selection, floating, degrees,
                          context.colors.ink(PointerButton::Secondary));
}

void SelectionTool::finish_rotate_drag(ToolContext& context, Point2f position) {
    rotate_to(context, position);
    context.ui.rotate.rotating = false;
    update_rotate_hover(context, position);
    m_drag = Drag::None;
    if (std::abs(m_rotate_degrees) < kMinRotateDegrees) return;

    // The turn is one edit, like the menu's rotate: undo hands back the
    // selection and the contents exactly as the drag found them.
    push_drag_edit(context);
}

bool SelectionTool::on_cancel(ToolContext& context) {
    // A gesture in progress is abandoned by putting the mask and the hovering
    // block back where the press found them: the same work for a marquee, a
    // contents move, a resize and a rotation, since all of them rebuild from
    // those snapshots.
    const bool dragging = m_drag != Drag::None;
    const bool lassoing = !m_polygon.empty();
    if (!dragging && !lassoing) return false;
    context.document.selection() = std::move(m_mask_before);
    context.document.floating() = std::move(m_float_before);
    context.document.mark_dirty(context.document.bounds());
    context.ui.rotate = {};
    context.ui.selection.points.clear();
    m_polygon.clear();
    m_drag = Drag::None;
    return true;
}

void SelectionTool::on_deactivate(ToolContext& context) { context.ui.rotate = {}; }

void SelectionTool::push_drag_edit(ToolContext& context) {
    SelectionEdit edit;
    edit.mask_before = std::move(m_mask_before);
    edit.float_before = std::move(m_float_before);
    edit.mask_after = context.document.selection();
    edit.float_after = context.document.floating();
    if (edit.empty()) return;
    context.history.push(make_selection_edit(context.document.layers().active_index(), std::move(edit)));
}

}  // namespace lsp
