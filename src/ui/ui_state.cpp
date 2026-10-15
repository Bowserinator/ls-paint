#include "ui/ui_state.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
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

// A second press on the same spot within this window counts as a double click.
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);
constexpr float kDoubleClickSlop = 4.0f;

// The picker draws every glyph in this box, whatever the size it shows it at.
constexpr int kGlyphBox = 26;

// How many kinds the ribbon box shows before the chevron expands it.
constexpr std::size_t kShapeRibbonCells = 14;

// Picker order, which the Pickup of the gallery follows: rows of six, starting
// with the line and the curve and ending with the bolt.
constexpr std::array<EShapeKind, 23> kShapeGalleryOrder{
    EShapeKind::LINE,          EShapeKind::CURVE,        EShapeKind::OVAL,
    EShapeKind::RECTANGLE,     EShapeKind::ROUNDEDRECTANGLE, EShapeKind::POLYGON,
    EShapeKind::TRIANGLE,      EShapeKind::RIGHTTRIANGLE, EShapeKind::DIAMOND,
    EShapeKind::PENTAGON,      EShapeKind::HEXAGON,      EShapeKind::ARROWRIGHT,
    EShapeKind::ARROWLEFT,     EShapeKind::ARROWUP,      EShapeKind::ARROWDOWN,
    EShapeKind::STAR4,         EShapeKind::STAR5,        EShapeKind::STAR6,
    EShapeKind::CALLOUTROUNDED, EShapeKind::CALLOUTOVAL, EShapeKind::CALLOUTCLOUD,
    EShapeKind::HEART,         EShapeKind::LIGHTNING};

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
        case ETool::SHAPE: return ToolId::Shape;
        case ETool::TEXT: return ToolId::Text;
        case ETool::ERASER: return ToolId::Eraser;
        case ETool::DROPPER: return ToolId::Dropper;
        case ETool::MAGNIFY: return ToolId::Magnify;
        case ETool::NONE: break;
    }
    return ToolId::None;
}

ShapeId shape_from_slint(EShapeKind kind) {
    switch (kind) {
        case EShapeKind::LINE: return ShapeId::Line;
        case EShapeKind::CURVE: return ShapeId::Curve;
        case EShapeKind::OVAL: return ShapeId::Oval;
        case EShapeKind::RECTANGLE: return ShapeId::Rectangle;
        case EShapeKind::ROUNDEDRECTANGLE: return ShapeId::RoundedRect;
        case EShapeKind::POLYGON: return ShapeId::Polygon;
        case EShapeKind::TRIANGLE: return ShapeId::Triangle;
        case EShapeKind::RIGHTTRIANGLE: return ShapeId::RightTriangle;
        case EShapeKind::DIAMOND: return ShapeId::Diamond;
        case EShapeKind::PENTAGON: return ShapeId::Pentagon;
        case EShapeKind::HEXAGON: return ShapeId::Hexagon;
        case EShapeKind::ARROWRIGHT: return ShapeId::ArrowRight;
        case EShapeKind::ARROWLEFT: return ShapeId::ArrowLeft;
        case EShapeKind::ARROWUP: return ShapeId::ArrowUp;
        case EShapeKind::ARROWDOWN: return ShapeId::ArrowDown;
        case EShapeKind::STAR4: return ShapeId::Star4;
        case EShapeKind::STAR5: return ShapeId::Star5;
        case EShapeKind::STAR6: return ShapeId::Star6;
        case EShapeKind::CALLOUTROUNDED: return ShapeId::CalloutRounded;
        case EShapeKind::CALLOUTOVAL: return ShapeId::CalloutOval;
        case EShapeKind::CALLOUTCLOUD: return ShapeId::CalloutCloud;
        case EShapeKind::HEART: return ShapeId::Heart;
        case EShapeKind::LIGHTNING: return ShapeId::Lightning;
    }
    return ShapeId::Rectangle;
}

ShapeMode mode_from_slint(EShapeMode mode) {
    switch (mode) {
        case EShapeMode::FILL: return ShapeMode::Fill;
        case EShapeMode::OUTLINE: return ShapeMode::Outline;
        case EShapeMode::BOTH: return ShapeMode::Both;
    }
    return ShapeMode::Outline;
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

    auto gallery = std::make_shared<slint::VectorModel<ShapeGlyph>>();
    auto ribbon = std::make_shared<slint::VectorModel<ShapeGlyph>>();
    for (std::size_t i = 0; i < kShapeGalleryOrder.size(); ++i) {
        const std::string commands =
            shape::gallery_commands(shape_from_slint(kShapeGalleryOrder[i]), {0, 0, kGlyphBox, kGlyphBox});
        ShapeGlyph glyph{slint::SharedString(commands), kShapeGalleryOrder[i]};
        gallery->push_back(glyph);
        if (i < kShapeRibbonCells) ribbon->push_back(glyph);
    }
    m_window.global<ShapeGallery>().set_items(gallery);
    m_window.global<ShapeGallery>().set_ribbon(ribbon);

    m_window.global<UserState>().on_commit_text([this](const slint::SharedString& text) {
        do_commit_text(std::string(text));
    });

    m_window.global<UserState>().on_pointer_down([this](float x, float y, bool secondary) {
        const auto& state = m_window.global<UserState>();
        // A click anywhere else closes an open text box first; the press that
        // dismissed it still reaches its tool.
        if (text_open()) do_commit_text(std::string(m_window.global<TextOverlay>().get_content()));
        sync_colors();
        sync_shape_style();
        m_controller.brush().size =
            std::max(1, static_cast<int>(std::lround(m_window.global<ToolOptions>().get_brush_size())));

        const ToolId tool = tool_from_slint(state.get_selected_tool());
        // Slint reports no click count, so a press on the same spot right after
        // the previous one is timed here as the double click that a polygon uses
        // to close itself.
        const auto now = std::chrono::steady_clock::now();
        const bool double_click =
            m_have_last_press && now - m_last_press < kDoubleClickWindow &&
            std::hypot(x - m_last_press_position.x, y - m_last_press_position.y) <= kDoubleClickSlop;
        m_have_last_press = true;
        m_last_press = now;
        m_last_press_position = {x, y};

        m_controller.input().pointer_down(tool, {x, y},
                                          secondary ? PointerButton::Secondary : PointerButton::Primary);
        if (double_click) m_controller.input().double_click(tool);

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
        m_controller.input().pointer_move(
            tool_from_slint(m_window.global<UserState>().get_selected_tool()), {x, y});
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
        // While a text box is open every key belongs to it, including the ones
        // that would otherwise be shortcuts.
        if (text_open()) return;
        // Enter and Escape end the active tool's own gesture (a polygon); the
        // shortcut table below only ever sees what that leaves behind.
        m_controller.input().key(
            tool_from_slint(m_window.global<UserState>().get_selected_tool()), std::string_view(text));
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

void UiState::sync_shape_style() {
    const auto& style = m_window.global<ShapeStyle>();
    ShapeOptions& options = m_controller.shape_options();
    options.kind = shape_from_slint(style.get_kind());
    options.mode = mode_from_slint(style.get_mode());
    options.thickness = style.get_thickness();
}

void UiState::sync_font_style() {
    const auto& style = m_window.global<FontStyle>();
    TextOptions& options = m_controller.text_options();
    options.family = std::string(style.get_family());
    options.size = style.get_size();
    options.bold = style.get_bold();
    options.italic = style.get_italic();
}

bool UiState::text_open() const noexcept { return m_controller.tool_ui().text.visible; }

void UiState::do_commit_text(const std::string& text) {
    const TextOverlayState state = m_controller.tool_ui().text;
    if (!state.visible) return;
    // Close the box first: an empty commit then leaves nothing behind, and the
    // overlay cannot come back when refresh() republishes the tool state.
    m_controller.input().deactivate(ToolId::Text);
    if (!text.empty()) {
        sync_font_style();
        m_controller.commit_text(state.origin, text, state.color);
    }
    // The commit both wrote pixels and closed the box, so push them out now
    // instead of waiting for whatever event comes next.
    refresh();
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

    // A tool that is left while it holds a gesture has to release it: nothing
    // else tells the shape tool that its polygon will never be finished.
    const ToolId tool = tool_from_slint(m_window.global<UserState>().get_selected_tool());
    if (tool != m_last_tool) {
        // The box goes with the tool: leaving it draws what was typed first.
        if (m_last_tool == ToolId::Text && m_window.global<TextOverlay>().get_visible())
            do_commit_text(std::string(m_window.global<TextOverlay>().get_content()));
        m_controller.input().deactivate(m_last_tool);
        m_last_tool = tool;
    }

    const ShapePreview& preview = m_controller.tool_ui().shape;
    auto& overlay = m_window.global<ShapeOverlay>();
    overlay.set_visible(preview.visible);
    overlay.set_commands(slint::SharedString(preview.commands));
    overlay.set_draw_fill(preview.draw_fill);
    overlay.set_draw_outline(preview.draw_outline);
    overlay.set_color(slint::Color::from_argb_uint8(preview.color.a, preview.color.r,
                                                    preview.color.g, preview.color.b));

    const std::string current = shape::gallery_commands(
        shape_from_slint(m_window.global<ShapeStyle>().get_kind()), {0, 0, kGlyphBox, kGlyphBox});
    m_window.global<ShapeGallery>().set_current_commands(slint::SharedString(current));

    const TextOverlayState& text = m_controller.tool_ui().text;
    auto& text_box = m_window.global<TextOverlay>();
    text_box.set_visible(text.visible);
    // A box that has just opened starts empty; after that the editor owns it.
    if (text.visible && !m_text_open) text_box.set_content(slint::SharedString());
    m_text_open = text.visible;
    text_box.set_x(text.origin.x);
    text_box.set_y(text.origin.y);
    text_box.set_color(
        slint::Color::from_argb_uint8(text.color.a, text.color.r, text.color.g, text.color.b));

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
