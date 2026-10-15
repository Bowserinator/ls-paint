#include "tools/selection_tool.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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

float distance_to(Point2f a, Point2f b) { return std::hypot(a.x - b.x, a.y - b.y); }

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
        if (distance_to(handle_anchor(bounds, handle), position) <= kHandleHitRadius) return handle;
    return std::nullopt;
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
    if (button == PointerButton::Secondary) return;  // reserved for the context menu

    const SelectionShape shape = context.shape;
    SelectionMask& selection = context.document.selection();

    if (shape == SelectionShape::Polygon && !m_polygon.empty()) {
        if (m_polygon.size() >= 3 && distance_to(position, m_polygon.front()) <= kClosePolygonDistance) {
            context.commit_selection();
            selection.fill_polygon(m_polygon);
            m_polygon.clear();
        } else {
            m_polygon.push_back(position);
        }
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
            m_dx = m_dy = 0;
            m_mask_before = selection;
            m_float_before = context.document.floating();
            m_drag = Drag::Contents;
            return;
        }
    }

    // A new selection replaces the old one, which applies a floating one first.
    context.commit_selection();

    if (shape == SelectionShape::Polygon) {
        m_polygon.push_back(position);
        return;
    }

    m_anchor = position;
    m_drag = Drag::Marquee;
    if (shape == SelectionShape::Freehand) m_polygon = {position};
}

void SelectionTool::on_pointer_move(ToolContext& context, Point2f position) {
    SelectionMask& selection = context.document.selection();

    if (m_drag == Drag::Contents) {
        const int dx = static_cast<int>(std::lround(position.x - m_contents_start.x));
        const int dy = static_cast<int>(std::lround(position.y - m_contents_start.y));
        if (dx == m_dx && dy == m_dy) return;

        // The lift only happens once the contents actually move: a press that
        // never travels leaves the layer — and the history — alone.
        std::optional<FloatingContents>& floating = context.document.floating();
        if (!floating)
            lift_contents(context.document.active_layer().pixels, selection, floating,
                          context.colors.ink(PointerButton::Secondary));
        if (floating) translate_contents(selection, *floating, dx - m_dx, dy - m_dy);

        m_dx = dx;
        m_dy = dy;
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

    if (m_drag != Drag::Marquee) return;
    if (context.shape == SelectionShape::Rectangular)
        context.document.selection().fill_rect(rect_from_corners(m_anchor, position));
    else
        m_polygon.push_back(position);
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
    if (m_drag != Drag::Marquee) return;

    if (context.shape == SelectionShape::Rectangular)
        context.document.selection().fill_rect(rect_from_corners(m_anchor, position));
    else
        context.document.selection().fill_polygon(m_polygon);

    m_polygon.clear();
    m_drag = Drag::None;
}

void SelectionTool::finish_contents_drag(ToolContext& context) {
    SelectionMask& selection = context.document.selection();
    std::optional<FloatingContents>& floating = context.document.floating();

    // A drag that ends where it began never happened: put the contents back
    // rather than leaving a lift that shows nothing new.
    if (m_dx == 0 && m_dy == 0 && !m_float_before && floating) floating.reset();

    SelectionEdit edit;
    edit.mask_before = std::move(m_mask_before);
    edit.float_before = std::move(m_float_before);
    edit.mask_after = selection;
    edit.float_after = floating;

    if (!edit.empty()) {
        const std::size_t layer_index = context.document.layers().active_index();
        context.document.mark_dirty(context.document.bounds());
        context.history.push(make_selection_edit(layer_index, std::move(edit)));
    }
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
    if (m_float_before) {
        SelectionEdit edit;
        edit.mask_before = std::move(m_mask_before);
        edit.float_before = std::move(m_float_before);
        edit.mask_after = selection;
        edit.float_after = floating;
        if (!edit.empty()) {
            const std::size_t layer_index = context.document.layers().active_index();
            context.history.push(make_selection_edit(layer_index, std::move(edit)));
        }
    }

    context.document.mark_dirty(context.document.bounds());
    m_drag = Drag::None;
}

}  // namespace lsp
