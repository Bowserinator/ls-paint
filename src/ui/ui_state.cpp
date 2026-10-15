#include "ui/ui_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "ui/shortcuts.hpp"

namespace lsp {

namespace {

// Marching ants: alternating light and dark runs three pixels long, advanced
// one pixel per tick. The whole pattern is rebuilt from the selection's
// border on every tick, so the animation is just the phase moving forward.
constexpr int kAntSegment = 3;
constexpr int kAntPeriod = kAntSegment * 2;
constexpr auto kAntTick = std::chrono::milliseconds(80);

// Mirrors SelectionTool's minimum selection span: below this the grips would
// swallow most of the "drag inside to move it" target.
constexpr int kMinGripSpan = 8;

// Walks the selection's border as a closed polyline — top, right, bottom,
// left — and emits one unit run per border cell, coloured by the marching
// phase. The view draws each cell as a single screen pixel, so the ants weigh
// the same at every zoom instead of thickening with the magnification.
void append_border_runs(const RectI& bounds, int phase, std::vector<SelectionDash>& dark,
                        std::vector<SelectionDash>& light) {
    const int w = bounds.width;
    const int h = bounds.height;
    if (w <= 0 || h <= 0) return;

    const int perimeter = 2 * (w + h - 2);
    if (perimeter <= 0) {
        // A single pixel has no border to march around; outline it anyway.
        dark.push_back({static_cast<float>(bounds.x), static_cast<float>(bounds.y), 1.0f, 1.0f});
        return;
    }

    // The point at arc position s. Each edge excludes its far corner, so the
    // four together visit every border pixel once.
    auto point_at = [&bounds, w, h](int s) -> std::pair<int, int> {
        if (s < w - 1) return {bounds.x + s, bounds.y};
        s -= w - 1;
        if (s < h - 1) return {bounds.right() - 1, bounds.y + s};
        s -= h - 1;
        if (s < w - 1) return {bounds.right() - 1 - s, bounds.bottom() - 1};
        s -= w - 1;
        return {bounds.x, bounds.bottom() - 1 - s};
    };

    for (int s = 0; s < perimeter; ++s) {
        const auto [x, y] = point_at(s);
        const int offset = (((s - phase) % kAntPeriod) + kAntPeriod) % kAntPeriod;
        (offset < kAntSegment ? dark : light).push_back(
            {static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f});
    }
}

// The anchors the grips are centred on, matching SelectionTool's hit test so
// the box drawn lines up with the pixels the tool accepts a press on.
std::vector<SelectionGrip> grip_anchors(const RectI& bounds) {
    const float left = static_cast<float>(bounds.x);
    const float top = static_cast<float>(bounds.y);
    const float right = static_cast<float>(bounds.right() - 1);
    const float bottom = static_cast<float>(bounds.bottom() - 1);
    const float mid_x = left + (right - left) / 2.0f;
    const float mid_y = top + (bottom - top) / 2.0f;
    return {{left, top},    {right, top},     {right, bottom}, {left, bottom},
            {mid_x, top},   {right, mid_y},   {mid_x, bottom}, {left, mid_y}};
}

CanvasDocument::Rotation rotation_from_slint(ERotation rotation) {
    switch (rotation) {
        case ERotation::CCW90: return CanvasDocument::Rotation::Ccw90;
        case ERotation::R180: return CanvasDocument::Rotation::R180;
        case ERotation::CW90: break;
    }
    return CanvasDocument::Rotation::Cw90;
}

SelectionShape shape_from_slint(ESelectionType type) {
    switch (type) {
        case ESelectionType::FREEHAND: return SelectionShape::Freehand;
        case ESelectionType::POLYGON: return SelectionShape::Polygon;
        case ESelectionType::RECTANGULAR: break;
    }
    return SelectionShape::Rectangular;
}

ContentTransform transform_from_slint(EContentTransform transform) {
    switch (transform) {
        case EContentTransform::ROTATECCW90: return ContentTransform::RotateCcw90;
        case EContentTransform::ROTATE180: return ContentTransform::Rotate180;
        case EContentTransform::FLIPHORIZONTAL: return ContentTransform::FlipHorizontal;
        case EContentTransform::FLIPVERTICAL: return ContentTransform::FlipVertical;
        case EContentTransform::ROTATECW90: break;
    }
    return ContentTransform::RotateCw90;
}

ToolId tool_from_slint(ETool tool) {
    switch (tool) {
        case ETool::SELECTION: return ToolId::Selection;
        case ETool::RESIZE: return ToolId::Resize;
        case ETool::ROTATE: return ToolId::Rotate;
        case ETool::PENCIL: return ToolId::Pencil;
        case ETool::FILL: return ToolId::Fill;
        case ETool::TEXT: return ToolId::Text;
        case ETool::ERASER: return ToolId::Eraser;
        case ETool::DROPPER: return ToolId::Dropper;
        case ETool::MAGNIFY: return ToolId::Magnify;
        case ETool::NONE: break;
    }
    return ToolId::None;
}

Rgba8 to_rgba(slint::Color color) {
    // Ink is always opaque: an alpha-0 color makes every stroke invisible while
    // the pencil still appears to be drawing (M0 probe bug).
    return {color.red(), color.green(), color.blue(), 255};
}

}  // namespace

UiState::UiState(AppWindow& window, AppController& controller)
    : m_window(window),
      m_controller(controller),
      m_bridge(controller.document().width(), controller.document().height()) {
    auto palette = std::make_shared<slint::VectorModel<slint::Color>>();
    for (const Rgba8& color : kDefaultPalette)
        palette->push_back(slint::Color::from_rgb_uint8(color.r, color.g, color.b));
    for (int i = 0; i < kCustomColorSlots; ++i)
        palette->push_back(slint::Color::from_rgb_uint8(255, 255, 255));
    m_window.global<UserState>().set_color_palette(palette);

    m_window.global<UserState>().on_pointer_down([this](float x, float y, bool secondary) {
        const auto& state = m_window.global<UserState>();
        sync_colors();
        m_controller.brush().size =
            std::max(1, static_cast<int>(std::lround(m_window.global<ToolOptions>().get_brush_size())));

        m_controller.input().pointer_down(tool_from_slint(state.get_selected_tool()), {x, y},
                                          secondary ? PointerButton::Secondary : PointerButton::Primary);

        const ColorPair& colors = m_controller.colors();
        const Rgba8 primary_ink = colors.ink(PointerButton::Primary);
        const Rgba8 secondary_ink = colors.ink(PointerButton::Secondary);
        state.set_primary_color(
            slint::Color::from_argb_uint8(primary_ink.a, primary_ink.r, primary_ink.g, primary_ink.b));
        state.set_secondary_color(slint::Color::from_argb_uint8(secondary_ink.a, secondary_ink.r,
                                                                secondary_ink.g, secondary_ink.b));
        refresh();
    });

    m_window.global<UserState>().on_pointer_move([this](float x, float y) {
        m_controller.input().pointer_move({x, y});
        refresh();
    });

    m_window.global<UserState>().on_pointer_up([this](float x, float y) {
        m_controller.input().pointer_up({x, y});
        refresh();
    });

    m_window.global<UserState>().on_canvas_resize([this](float width, float height) {
        m_controller.resize_canvas(std::clamp(static_cast<int>(std::lround(width)), 1, kMaxCanvasDimension),
                                   std::clamp(static_cast<int>(std::lround(height)), 1, kMaxCanvasDimension),
                                   to_rgba(m_window.global<UserState>().get_secondary_color()));
        refresh();
    });

    m_window.global<UserState>().on_key_pressed([this](const slint::SharedString& text, bool ctrl, bool shift) {
        if (m_controller.input().active()) return;  // never act mid-stroke
        const auto action = lookup_shortcut(std::string_view(text), ctrl, shift);
        if (!action) return;
        sync_colors();  // the controller otherwise only sees colors set by a pointer press
        switch (*action) {
            case ShortcutAction::Undo: m_controller.history().undo(); break;
            case ShortcutAction::Redo: m_controller.history().redo(); break;
            case ShortcutAction::Cut: m_controller.cut_selection(); break;
            case ShortcutAction::Copy: m_controller.copy_selection(); break;
            case ShortcutAction::Paste: m_controller.paste_clipboard(); break;
            case ShortcutAction::SelectAll: m_controller.select_all(); break;
        }
        refresh();
    });

    m_window.global<UserState>().on_select_shape([this](ESelectionType type) {
        m_controller.set_selection_shape(shape_from_slint(type));
        m_window.global<UserState>().set_selection_type(type);
    });

    m_window.global<UserState>().on_select_all([this] { m_controller.select_all(); refresh(); });
    m_window.global<UserState>().on_invert_selection([this] { m_controller.invert_selection(); refresh(); });
    m_window.global<UserState>().on_clear_selection([this] { m_controller.clear_selection(); refresh(); });
    m_window.global<UserState>().on_transparent_selection([this] { m_controller.make_selection_transparent(); refresh(); });
    m_window.global<UserState>().on_crop_to_selection([this] { m_controller.crop_to_selection(); refresh(); });
    m_window.global<UserState>().on_rotate([this](ERotation rotation) { m_controller.rotate(rotation_from_slint(rotation)); refresh(); });
    m_window.global<UserState>().on_flip_horizontal([this] { m_controller.flip_horizontal(); refresh(); });
    m_window.global<UserState>().on_flip_vertical([this] { m_controller.flip_vertical(); refresh(); });

    m_window.global<UserState>().on_cut_selection([this] {
        sync_colors();
        m_controller.cut_selection();
        refresh();
    });
    m_window.global<UserState>().on_copy_selection([this] { m_controller.copy_selection(); refresh(); });
    m_window.global<UserState>().on_paste_selection([this] { m_controller.paste_clipboard(); refresh(); });
    m_window.global<UserState>().on_transform_selection_content(
        [this](EContentTransform transform) {
            sync_colors();
            m_controller.transform_selection_contents(transform_from_slint(transform));
            refresh();
        });

    refresh();

    // The outline crawls on its own: nothing else has to touch it.
    m_ants_timer.start(slint::TimerMode::Repeated, kAntTick, [this] {
        m_ant_phase = (m_ant_phase + 1) % kAntPeriod;
        update_ants();
    });
}

void UiState::sync_colors() {
    const auto& state = m_window.global<UserState>();
    m_controller.colors() = {to_rgba(state.get_primary_color()), to_rgba(state.get_secondary_color())};
}

void UiState::refresh() {
    CanvasDocument& document = m_controller.document();
    if (const auto region = document.flush()) {
        m_bridge.upload(document.flattened(), *region);
        m_window.set_texture(m_bridge.image());
    }

    auto& state = m_window.global<DocumentState>();
    state.set_canvas_width(document.width());
    state.set_canvas_height(document.height());

    const SelectionMask& selection = document.selection();
    const RectI bounds = selection.bounds();
    state.set_has_selection(selection.active() && !bounds.empty());
    state.set_has_clipboard(!m_controller.clipboard().empty());
    state.set_selection_x(bounds.x);
    state.set_selection_y(bounds.y);
    state.set_selection_width(bounds.width);
    state.set_selection_height(bounds.height);

    update_ants();
}

void UiState::update_ants() {
    auto& state = m_window.global<DocumentState>();
    CanvasDocument& document = m_controller.document();
    const SelectionMask& selection = document.selection();
    const RectI bounds = selection.bounds();
    const bool visible = selection.active() && !bounds.empty();

    if (!visible) {
        // Written only when a selection actually disappears: this runs on a
        // timer, and assigning fresh models every tick would repaint forever.
        if (m_ants_visible) {
            m_ants_visible = false;
            state.set_ants_dark(
                std::make_shared<slint::VectorModel<SelectionDash>>(std::vector<SelectionDash>{}));
            state.set_ants_light(
                std::make_shared<slint::VectorModel<SelectionDash>>(std::vector<SelectionDash>{}));
            state.set_grips(
                std::make_shared<slint::VectorModel<SelectionGrip>>(std::vector<SelectionGrip>{}));
        }
        return;
    }
    m_ants_visible = true;

    std::vector<SelectionDash> dark;
    std::vector<SelectionDash> light;
    append_border_runs(bounds, m_ant_phase, dark, light);
    state.set_ants_dark(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(dark)));
    state.set_ants_light(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(light)));

    std::vector<SelectionGrip> grips;
    if (bounds.width >= kMinGripSpan && bounds.height >= kMinGripSpan &&
        m_window.global<UserState>().get_selected_tool() == ETool::SELECTION) {
        grips = grip_anchors(bounds);
    }
    state.set_grips(std::make_shared<slint::VectorModel<SelectionGrip>>(std::move(grips)));
}

}  // namespace lsp
