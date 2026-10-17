#include "ui/ui_state.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "core/color.hpp"
#include "io/document_io.hpp"
#include "text/font_catalog.hpp"
#include "tools/text_tool.hpp"
#include "ui/shortcuts.hpp"

namespace lsp {

namespace {

// The canvas size fields hold digits and nothing else, so anything else is a
// size the user has not finished typing.
[[nodiscard]] int parse_canvas_size(const slint::SharedString& text) {
    const std::string value(text);
    if (value.empty() || value.size() > 9) return 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return 0;
    }
    const long long size = std::stoll(value);
    if (size < 1 || size > kMaxCanvasDimension) return 0;
    return static_cast<int>(size);
}

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

// Walks the polyline a selection gesture has drawn so far and emits one unit
// run per cell, so an unfinished lasso or polygon shows the same crawling
// outline the finished selection gets.
void append_path_runs(const std::vector<Point2f>& points, int phase, std::vector<SelectionDash>& dark,
                      std::vector<SelectionDash>& light) {
    if (points.size() < 2) return;

    int step = 0;
    int last_x = 0;
    int last_y = 0;
    bool have_last = false;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const int x0 = static_cast<int>(std::lround(points[i - 1].x));
        const int y0 = static_cast<int>(std::lround(points[i - 1].y));
        const int x1 = static_cast<int>(std::lround(points[i].x));
        const int y1 = static_cast<int>(std::lround(points[i].y));
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        // The segment's first cell is the previous segment's last one.
        for (int k = (i == 1 ? 0 : 1); k <= steps; ++k) {
            const int x = steps == 0 ? x0 : x0 + (x1 - x0) * k / steps;
            const int y = steps == 0 ? y0 : y0 + (y1 - y0) * k / steps;
            if (have_last && x == last_x && y == last_y) continue;
            last_x = x;
            last_y = y;
            have_last = true;
            const int offset = (((step - phase) % kAntPeriod) + kAntPeriod) % kAntPeriod;
            (offset < kAntSegment ? dark : light).push_back(
                {static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f});
            ++step;
        }
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
        case ETool::BRUSH: return ToolId::Brush;
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

BrushKind brush_from_slint(EBrushKind kind) {
    switch (kind) {
        case EBrushKind::BRUSH: return BrushKind::Brush;
        case EBrushKind::CALLIGRAPHY1: return BrushKind::Calligraphy1;
        case EBrushKind::CALLIGRAPHY2: return BrushKind::Calligraphy2;
        case EBrushKind::AIRBRUSH: return BrushKind::Airbrush;
        case EBrushKind::OIL: return BrushKind::Oil;
        case EBrushKind::CRAYON: return BrushKind::Crayon;
        case EBrushKind::MARKER: return BrushKind::Marker;
        case EBrushKind::NATURALPENCIL: return BrushKind::NaturalPencil;
        case EBrushKind::WATERCOLOR: return BrushKind::Watercolor;
    }
    return BrushKind::Brush;
}

ShapeTexture texture_from_slint(EShapeTexture texture) {
    switch (texture) {
        case EShapeTexture::NONE: return ShapeTexture::None;
        case EShapeTexture::SOLID: return ShapeTexture::Solid;
        case EShapeTexture::CRAYON: return ShapeTexture::Crayon;
        case EShapeTexture::MARKER: return ShapeTexture::Marker;
        case EShapeTexture::OIL: return ShapeTexture::Oil;
        case EShapeTexture::NATURALPENCIL: return ShapeTexture::NaturalPencil;
        case EShapeTexture::WATERCOLOR: return ShapeTexture::Watercolor;
    }
    return ShapeTexture::None;
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
    m_palette = std::make_shared<slint::VectorModel<slint::Color>>();
    for (const Rgba8& color : kDefaultPalette)
        m_palette->push_back(slint::Color::from_rgb_uint8(color.r, color.g, color.b));
    for (int i = 0; i < kCustomColorSlots; ++i)
        m_palette->push_back(slint::Color::from_rgb_uint8(255, 255, 255));
    m_window.global<UserState>().set_color_palette(m_palette);

    m_font_families = text_font_families();
    publish_font_families("");

    auto gallery = std::make_shared<slint::VectorModel<ShapeGlyph>>();
    for (const EShapeKind kind : kShapeGalleryOrder) {
        const ShapeId id = shape_from_slint(kind);
        const std::string commands = shape::gallery_commands(id, {0, 0, kGlyphBox, kGlyphBox});
        gallery->push_back(ShapeGlyph{slint::SharedString(commands), kind, shape::is_closed(id)});
    }
    m_window.global<ShapeGallery>().set_items(gallery);

    m_window.global<UserState>().on_commit_text([this](const slint::SharedString& text) {
        do_commit_text(std::string(text));
    });
    m_window.global<UserState>().on_cancel_text([this] { do_cancel_text(); });
    m_window.global<UserState>().on_measure_text([this](const slint::SharedString& text) {
        publish_line_widths(std::string_view(text));
    });

    m_window.global<FontStyle>().on_filter_families([this](const slint::SharedString& query) {
        publish_font_families(std::string_view(query));
    });
    m_window.global<FontStyle>().on_family_changed([this] {
        const auto& style = m_window.global<FontStyle>();
        style.set_preview_scale(text_preview_scale(std::string_view(style.get_family())));
    });
    m_window.global<FontStyle>().set_preview_scale(
        text_preview_scale(std::string_view(m_window.global<FontStyle>().get_family())));

    m_window.global<UserState>().on_pointer_down([this](float x, float y, bool secondary) {
        const auto& state = m_window.global<UserState>();
        // A click anywhere else closes an open text box first; the press that
        // dismissed it still reaches its tool.
        if (text_open()) do_commit_text(std::string(m_window.global<TextOverlay>().get_content()));
        sync_colors();
        sync_shape_style();
        auto& tool_options = m_window.global<ToolOptions>();
        m_controller.brush().size =
            std::max(1, static_cast<int>(std::lround(tool_options.get_brush_size())));
        m_controller.brush().shape =
            tool_options.get_brush_square() ? BrushShape::Square : BrushShape::Round;
        m_controller.brush().spacing = tool_options.get_brush_spacing();
        // Only the brush tool paints with the picked brush type. Every other
        // stroke tool, the eraser included, uses the plain tip.
        m_controller.brush().kind = state.get_selected_tool() == ETool::BRUSH
                                        ? brush_from_slint(tool_options.get_brush_kind())
                                        : BrushKind::Brush;

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
            case ShortcutAction::Paste: do_paste(); break;
            case ShortcutAction::SelectAll: m_controller.select_all(); break;
            case ShortcutAction::Deselect: m_controller.clear_selection(); break;
            case ShortcutAction::New: do_file_new(); break;
            case ShortcutAction::Open: do_file_open(); break;
            case ShortcutAction::Save: do_file_save(); break;
            case ShortcutAction::SaveAs: do_file_save_as(); break;
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
    m_window.global<UserState>().on_paste_selection([this] { do_paste(); });
    m_window.global<UserState>().on_transform_selection_content(
        [this](EContentTransform transform) {
            sync_colors();
            m_controller.transform_selection_contents(transform_from_slint(transform));
            refresh();
        });

    m_window.global<UserState>().on_file_new([this] { do_file_new(); });
    m_window.global<UserState>().on_file_open([this] { do_file_open(); });
    m_window.global<UserState>().on_file_save([this] { do_file_save(); });
    m_window.global<UserState>().on_file_save_as([this] { do_file_save_as(); });
    m_window.global<UserState>().on_file_exit([this] { do_file_exit(); });
    m_window.global<UserState>().on_paste_from_file([this] { do_paste_from_file(); });

    auto& dialog = m_window.global<DialogState>();
    dialog.on_file_up([this] { do_file_up(); });
    dialog.on_file_navigate([this](int row) { do_file_navigate(row); });
    dialog.on_file_filter_changed([this](int index) { do_file_filter_changed(index); });
    dialog.on_file_ok([this] { do_file_ok(); });
    dialog.on_file_cancel([this] { do_file_cancel(); });
    dialog.on_resize_ok([this] { do_resize_ok(); });
    dialog.on_resize_cancel([this] { do_resize_cancel(); });
    m_window.global<UserState>().on_resize_dialog([this] { do_open_resize_dialog(); });
    m_window.global<UserState>().on_edit_colors([this] { do_open_color_dialog(); });
    m_window.global<UserState>().on_about([this] { do_open_about(); });

    dialog.on_confirm_save([this, &dialog] {
        dialog.set_confirm_visible(false);
        m_window.invoke_focus_app();
        if (!m_controller.has_current_path()) {
            // The pending action waits while the picker asks where to put it.
            show_file_dialog(EFileDialogMode::SAVE);
            return;
        }
        if (const IoResult result = m_controller.save_document(); !result.ok) {
            show_message("Save failed", result.error);
            return;
        }
        set_title();
        run_pending();
    });
    dialog.on_confirm_discard([this, &dialog] {
        dialog.set_confirm_visible(false);
        m_window.invoke_focus_app();
        run_pending();
    });
    dialog.on_confirm_cancel([this, &dialog] {
        dialog.set_confirm_visible(false);
        m_window.invoke_focus_app();
        m_pending = PendingAction::None;
    });
    dialog.on_message_ok([this, &dialog] {
        dialog.set_message_visible(false);
        m_window.invoke_focus_app();
    });

    // The window's close button asks the same question as File > Exit.
    m_window.window().on_close_requested([this] {
        if (!m_controller.history().is_modified()) return slint::CloseRequestResponse::HideWindow;
        do_file_exit();
        return slint::CloseRequestResponse::KeepWindowShown;
    });

    refresh();

    // The outline crawls on its own: nothing else has to touch it.
    m_ants_timer.start(slint::TimerMode::Repeated, kAntTick, [this] {
        m_ant_phase = (m_ant_phase + 1) % kAntPeriod;
        update_ants();
    });
}

void UiState::set_palette_color(int index, const slint::Color& color) {
    if (!m_palette || index < 0 || index >= static_cast<int>(m_palette->row_count())) return;
    m_palette->set_row_data(static_cast<std::size_t>(index), color);
}

// The dialog starts with the ribbon's strip in its grid and empty slots after
// it, and is free to change them from then on.
void UiState::seed_dialog_customs() {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    m_dialog_customs = std::make_shared<slint::VectorModel<slint::Color>>();
    for (int i = 0; i < kDialogCustomColorSlots; ++i) {
        slint::Color color = slint::Color::from_rgb_uint8(255, 255, 255);
        if (i < kCustomColorSlots && m_palette) {
            const std::size_t row = kDefaultPalette.size() + static_cast<std::size_t>(i);
            if (const std::optional<slint::Color> stored = m_palette->row_data(row)) color = *stored;
        }
        m_dialog_customs->push_back(color);
    }
    picker->set_custom_colors(m_dialog_customs);
}

void UiState::write_dialog_custom(int index, const slint::Color& color) {
    if (!m_dialog_customs || index < 0 || index >= kDialogCustomColorSlots) return;
    m_dialog_customs->set_row_data(static_cast<std::size_t>(index), color);
}

// The strip keeps the last ten colors, so a new one enters at its head and the
// oldest leaves.
void UiState::rotate_strip(const slint::Color& color) {
    if (!m_palette) return;
    std::vector<slint::Color> kept;
    kept.reserve(static_cast<std::size_t>(kCustomColorSlots));
    for (int i = 1; i < kCustomColorSlots; ++i) {
        const std::size_t row = kDefaultPalette.size() + static_cast<std::size_t>(i);
        const std::optional<slint::Color> stored = m_palette->row_data(row);
        kept.push_back(stored.value_or(slint::Color::from_rgb_uint8(255, 255, 255)));
    }
    set_palette_color(static_cast<int>(kDefaultPalette.size()), color);
    for (std::size_t i = 0; i < kept.size(); ++i) {
        set_palette_color(static_cast<int>(kDefaultPalette.size() + i + 1), kept[i]);
    }
}

// The color window is made on first use: a window of its own, so the window
// manager moves, resizes and closes it.
void UiState::ensure_color_window() {
    if (m_color_window) return;
    m_color_window = lsp_colors::EditColorsWindow::create();
    lsp_colors::EditColorsWindow& window = **m_color_window;
    // The dialog keeps its own grid of custom colors, which the ribbon's strip
    // seeds.
    seed_dialog_customs();
    window.on_accepted([this] { do_color_accepted(); });
    window.on_cancelled([this] { do_color_cancelled(); });
    window.on_add_custom([this] { do_color_add_custom(); });
    window.on_basic_picked([this](const slint::Color& color) { do_color_basic_picked(color); });
    window.on_custom_picked(
        [this](const slint::Color& color, int index) { do_color_custom_picked(color, index); });
    window.on_ring_picked([this](float x, float y) { do_color_ring_picked(x, y); });
    window.on_square_picked([this](float x, float y) { do_color_square_picked(x, y); });
    window.on_color_entered(
        [this](const slint::SharedString& text) { do_color_entered(text); });
    window.on_hex_edited(
        [this](const slint::SharedString& text) { do_color_hex_edited(text); });
    // Closing the window from its own title bar leaves the colors alone, which
    // is what Cancel does, so the default of hiding the window is enough.
}

lsp_colors::EditColorsWindow* UiState::color_picker() {
    return m_color_window ? &**m_color_window : nullptr;
}

lsp_colors::EditColorsWindow& UiState::color_window() {
    ensure_color_window();
    return **m_color_window;
}

bool UiState::color_window_open() const noexcept {
    return m_color_window && (*m_color_window)->window().is_visible();
}

void UiState::do_open_color_dialog() {
    ensure_color_window();
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    const auto& state = m_window.global<UserState>();
    // The dialog edits whichever of the two colors is active.
    const slint::Color active =
        state.get_color_idx() == 0 ? state.get_primary_color() : state.get_secondary_color();
    const Hsv hsv = hsv_from_rgb(to_rgba(active));
    picker->set_hue(hsv.hue);
    picker->set_saturation(hsv.saturation);
    picker->set_value(hsv.value);
    // Every open centers the window again.
    push_color_dialog();
    (*m_color_window)->show();
}

// Writes the color, the color the square shades and the readouts from the
// hue, saturation and value the dialog was left on.
void UiState::push_color_dialog() {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    const Hsv hsv{wrap_hue(picker->get_hue()), std::clamp(picker->get_saturation(), 0.0f, 1.0f),
                  std::clamp(picker->get_value(), 0.0f, 1.0f)};
    const Rgba8 color = rgb_from_hsv(hsv);
    const Rgba8 hue_color = rgb_from_hsv(Hsv{hsv.hue, 1.0f, 1.0f});
    picker->set_hue(hsv.hue);
    picker->set_saturation(hsv.saturation);
    picker->set_value(hsv.value);
    picker->set_current(slint::Color::from_argb_uint8(color.a, color.r, color.g, color.b));
    picker->set_hue_color(
        slint::Color::from_argb_uint8(hue_color.a, hue_color.r, hue_color.g, hue_color.b));
    picker->set_red_text(slint::SharedString(std::to_string(color.r)));
    picker->set_green_text(slint::SharedString(std::to_string(color.g)));
    picker->set_blue_text(slint::SharedString(std::to_string(color.b)));
    // RRGGBB in capitals, with no leading hash: the box is labelled Hex.
    std::string hex = to_hex(color).substr(1);
    std::ranges::transform(hex, hex.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    picker->set_hex_text(slint::SharedString(hex));
    picker->set_hue_text(
        slint::SharedString(std::to_string(static_cast<int>(std::lround(hsv.hue)))));
    picker->set_sat_text(slint::SharedString(
        std::to_string(static_cast<int>(std::lround(hsv.saturation * 100.0f)))));
    picker->set_lum_text(slint::SharedString(
        std::to_string(static_cast<int>(std::lround(hsv.value * 100.0f)))));
}

void UiState::do_color_accepted() {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    auto& state = m_window.global<UserState>();
    if (state.get_color_idx() == 0) {
        state.set_primary_color(picker->get_current());
    } else {
        state.set_secondary_color(picker->get_current());
    }
    // The slot the dialog points at goes into the ribbon's strip, which keeps
    // the last ten colors.
    const int slot = std::clamp(picker->get_custom_index(), 0, kDialogCustomColorSlots - 1);
    if (m_dialog_customs) {
        const std::optional<slint::Color> chosen =
            m_dialog_customs->row_data(static_cast<std::size_t>(slot));
        if (chosen) rotate_strip(*chosen);
    }
    (*m_color_window)->hide();
    m_window.invoke_focus_app();
    sync_colors();
    refresh();
}

void UiState::do_color_cancelled() {
    if (m_color_window) (*m_color_window)->hide();
    m_window.invoke_focus_app();
}

void UiState::do_color_add_custom() {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    const int slot = std::clamp(picker->get_custom_index(), 0, kDialogCustomColorSlots - 1);
    write_dialog_custom(slot, picker->get_current());
    // The next add fills the next slot, as Paint walks the grid.
    picker->set_custom_index((slot + 1) % kDialogCustomColorSlots);
}

void UiState::do_color_basic_picked(const slint::Color& color) {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    const Hsv hsv = hsv_from_rgb(to_rgba(color));
    picker->set_hue(hsv.hue);
    picker->set_saturation(hsv.saturation);
    picker->set_value(hsv.value);
    push_color_dialog();
}

void UiState::do_color_custom_picked(const slint::Color& color, int index) {
    color_picker()->set_custom_index(std::clamp(index, 0, kCustomColorSlots - 1));
    do_color_basic_picked(color);
}

void UiState::do_color_ring_picked(float x, float y) {
    const float dx = x - 0.5f;
    const float dy = y - 0.5f;
    if (dx == 0.0f && dy == 0.0f) return;
    // Zero degrees is the top of the ring and the hue grows clockwise, which is
    // how the conic gradient the ring is painted with reads.
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    picker->set_hue(wrap_hue(std::atan2(dx, -dy) * (180.0f / std::numbers::pi_v<float>)));
    push_color_dialog();
}

void UiState::do_color_square_picked(float x, float y) {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    picker->set_saturation(std::clamp(x, 0.0f, 1.0f));
    picker->set_value(std::clamp(1.0f - y, 0.0f, 1.0f));
    push_color_dialog();
}

void UiState::do_color_entered(const slint::SharedString& text) {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    const std::optional<Rgba8> parsed = parse_color(std::string_view(text));
    if (!parsed) {
        // An entry that is not a color leaves the picker on the color it had.
        push_color_dialog();
        return;
    }
    const Hsv hsv = hsv_from_rgb(*parsed);
    picker->set_hue(hsv.hue);
    picker->set_saturation(hsv.saturation);
    picker->set_value(hsv.value);
    push_color_dialog();
}

// The hex box was typed or pasted into. A pasted "#333" loses its hash and
// becomes 333333 at once; a full six digits set the color as the last one lands.
void UiState::do_color_hex_edited(const slint::SharedString& text) {
    const std::string cleaned = clean_hex_entry(std::string_view(text));
    if (cleaned != std::string_view(text)) {
        color_picker()->set_hex_text(slint::SharedString(cleaned));
    }
    if (cleaned.size() == 6 && parse_color(cleaned)) {
        do_color_entered(slint::SharedString(cleaned));
    }
}

void UiState::ensure_about_window() {
    if (m_about_window) return;
    m_about_window = lsp_about::AboutWindow::create();
    (**m_about_window).on_closed([this] {
        if (m_about_window) (*m_about_window)->hide();
    });
}

void UiState::do_open_about() {
    ensure_about_window();
    (*m_about_window)->show();
}

void UiState::sync_colors() {
    const auto& state = m_window.global<UserState>();
    m_controller.colors() = {to_rgba(state.get_primary_color()), to_rgba(state.get_secondary_color())};
}

void UiState::sync_shape_style() {
    const auto& style = m_window.global<ShapeStyle>();
    ShapeOptions& options = m_controller.shape_options();
    options.kind = shape_from_slint(style.get_kind());
    options.outline = texture_from_slint(style.get_outline());
    options.fill = texture_from_slint(style.get_fill());
    options.thickness = style.get_thickness();
}

void UiState::publish_font_families(std::string_view query) {
    const std::string needle = font_family_key(query);
    auto families = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const std::string& family : m_font_families) {
        if (!needle.empty() && font_family_key(family).find(needle) == std::string::npos) continue;
        families->push_back(slint::SharedString(family));
    }
    m_window.global<FontStyle>().set_families(families);
}

void UiState::publish_line_widths(std::string_view text) {
    // The controls are read first: the widths belong to the face and size the
    // preview is drawn with, whatever they are set to right now.
    sync_font_style();
    auto widths = std::make_shared<slint::VectorModel<float>>();
    for (const float width : text_line_widths(text, m_controller.text_options())) {
        widths->push_back(width);
    }
    m_window.global<TextOverlay>().set_line_widths(widths);
}

void UiState::publish_hover_color() {
    auto& state = m_window.global<UserState>();
    const PixelBuffer& paper = m_controller.document().flattened();
    const int x = static_cast<int>(std::floor(state.get_cursor_x()));
    const int y = static_cast<int>(std::floor(state.get_cursor_y()));
    if (!state.get_cursor_on_canvas() || !paper.contains(x, y)) {
        state.set_hover_color_text(slint::SharedString());
        return;
    }

    const Rgba8 pixel = paper.at(x, y);
    // The readout is upper case, the way Paint prints a colour.
    std::string hex = to_hex(pixel);
    for (char& digit : hex) {
        digit = static_cast<char>(std::toupper(static_cast<unsigned char>(digit)));
    }
    state.set_hover_color_text(
        slint::SharedString("(" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," +
                            std::to_string(pixel.b) + ") " + hex));
}

void UiState::sync_font_style() {
    const auto& style = m_window.global<FontStyle>();
    TextOptions& options = m_controller.text_options();
    options.family = std::string(style.get_family());
    options.size = style.get_size();
    options.bold = style.get_bold();
    options.italic = style.get_italic();
    options.underline = style.get_underline();
    options.strikethrough = style.get_strike();
    options.opaque_background = style.get_opaque_background();
    options.background = to_rgba(m_window.global<UserState>().get_secondary_color());
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

void UiState::do_cancel_text() {
    if (!text_open()) return;
    // Close the box and drop what was typed: nothing reaches the paper.
    m_controller.input().deactivate(ToolId::Text);
    m_window.global<TextOverlay>().set_content(slint::SharedString());
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

    publish_hover_color();

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
    overlay.set_outline_color(slint::Color::from_argb_uint8(
        preview.outline_color.a, preview.outline_color.r, preview.outline_color.g,
        preview.outline_color.b));
    overlay.set_fill_color(slint::Color::from_argb_uint8(
        preview.fill_color.a, preview.fill_color.r, preview.fill_color.g, preview.fill_color.b));


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
    // A box that has just opened has no rules to draw yet, and one that has
    // closed must not keep the last box's.
    publish_line_widths(text.visible ? std::string_view(text_box.get_content())
                                     : std::string_view{});

    set_title();
    update_ants();
}

void UiState::update_ants() {
    auto& state = m_window.global<DocumentState>();
    CanvasDocument& document = m_controller.document();
    const SelectionMask& selection = document.selection();
    const RectI bounds = selection.bounds();
    const bool visible = selection.active() && !bounds.empty();
    // A gesture that is still being drawn has no mask yet, so its outline comes
    // from the path the tool has published so far.
    const SelectionPreview& preview = m_controller.tool_ui().selection;
    const bool drawing = preview.points.size() >= 2;

    if (!visible && !drawing) {
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
    if (visible) append_border_runs(bounds, m_ant_phase, dark, light);
    if (drawing) append_path_runs(preview.points, m_ant_phase, dark, light);
    state.set_ants_dark(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(dark)));
    state.set_ants_light(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(light)));

    std::vector<SelectionGrip> grips;
    if (visible && bounds.width >= kMinGripSpan && bounds.height >= kMinGripSpan &&
        m_window.global<UserState>().get_selected_tool() == ETool::SELECTION) {
        grips = grip_anchors(bounds);
    }
    state.set_grips(std::make_shared<slint::VectorModel<SelectionGrip>>(std::move(grips)));
}

void UiState::set_title() {
    auto& state = m_window.global<DocumentState>();
    state.set_title(slint::SharedString(m_controller.display_name()));
    state.set_modified(m_controller.history().is_modified());
}
void UiState::after_document_change() {
    // A document nobody has moved around yet starts at its own size.
    m_window.global<UserState>().set_zoom(1.0f);
    sync_colors();
    set_title();
    refresh();
}

void UiState::ask_unsaved(PendingAction action) {
    m_pending = action;
    auto& dialog = m_window.global<DialogState>();
    dialog.set_confirm_message(slint::SharedString("Do you want to save changes to " +
                                                  m_controller.display_name() + "?"));
    dialog.set_confirm_visible(true);
}

void UiState::run_pending() {
    const PendingAction action = m_pending;
    m_pending = PendingAction::None;
    switch (action) {
        case PendingAction::None: break;
        case PendingAction::New:
            (void)m_controller.new_document();
            after_document_change();
            break;
        case PendingAction::Open: show_file_dialog(EFileDialogMode::OPEN); break;
        case PendingAction::Exit: m_window.hide(); break;
    }
}

void UiState::do_file_new() {
    // New and Open both throw away what is on the paper, so both ask first.
    if (m_controller.history().is_modified()) {
        ask_unsaved(PendingAction::New);
        return;
    }
    m_pending = PendingAction::New;
    run_pending();
}

void UiState::do_file_open() {
    if (m_controller.history().is_modified()) {
        ask_unsaved(PendingAction::Open);
        return;
    }
    show_file_dialog(EFileDialogMode::OPEN);
}

void UiState::do_file_save() {
    if (!m_controller.has_current_path()) {
        show_file_dialog(EFileDialogMode::SAVE);
        return;
    }
    if (const IoResult result = m_controller.save_document(); !result.ok) {
        show_message("Save failed", result.error);
        return;
    }
    set_title();
}

void UiState::do_file_save_as() { show_file_dialog(EFileDialogMode::SAVE); }

void UiState::do_file_exit() {
    if (m_controller.history().is_modified()) {
        ask_unsaved(PendingAction::Exit);
        return;
    }
    m_window.hide();
}

void UiState::do_paste() {
    // A selection copied in the app pastes as it always did; an empty in-app
    // clipboard falls back to the image the system clipboard holds.
    if (!m_controller.clipboard().empty()) {
        m_controller.paste_clipboard();
    } else {
        const IoResult result = m_controller.paste_from_clipboard();
        if (!result.ok) {
            std::fprintf(stderr, "Paste failed: %s\n", result.error.c_str());
            return;
        }
    }
    // A pasted block stays a floating selection until it is applied, so hand it
    // to the selection tool: that is what gives it its grips.
    m_window.global<UserState>().set_selected_tool(ETool::SELECTION);
    refresh();
}

void UiState::do_paste_from_file() { show_file_dialog(EFileDialogMode::PASTE); }

void UiState::show_message(const std::string& title, const std::string& text) {
    auto& dialog = m_window.global<DialogState>();
    dialog.set_message_title(slint::SharedString(title));
    dialog.set_message_text(slint::SharedString(text));
    dialog.set_message_visible(true);
}

void UiState::show_file_dialog(EFileDialogMode mode) {
    auto& dialog = m_window.global<DialogState>();
    dialog.set_file_mode(mode);
    dialog.set_file_has_error(false);
    dialog.set_file_error(slint::SharedString());
    dialog.set_file_filter(0);

    switch (mode) {
        case EFileDialogMode::OPEN:
            dialog.set_file_title(slint::SharedString("Open"));
            dialog.set_file_accept_text(slint::SharedString("Open"));
            dialog.set_file_path(slint::SharedString());
            m_file_dir = m_controller.default_directory();
            break;
        case EFileDialogMode::SAVE: {
            dialog.set_file_title(slint::SharedString("Save As"));
            dialog.set_file_accept_text(slint::SharedString("Save"));
            const ImageFormat format = format_from_index(dialog.get_file_format());
            const std::string stem = m_controller.has_current_path()
                                         ? m_controller.current_path().stem().string()
                                         : std::string("untitled");
            dialog.set_file_path(slint::SharedString(stem + extension_for(format)));
            m_file_dir = m_controller.default_directory();
            break;
        }
        case EFileDialogMode::PASTE:
            dialog.set_file_title(slint::SharedString("Paste from file"));
            dialog.set_file_accept_text(slint::SharedString("Paste"));
            dialog.set_file_path(slint::SharedString());
            m_file_dir = m_controller.default_directory();
            break;
    }

    dialog.set_file_visible(true);
    refresh_file_dialog();
}

void UiState::refresh_file_dialog() {
    auto& dialog = m_window.global<DialogState>();
    dialog.set_file_directory(slint::SharedString(m_file_dir.string()));

    const bool images_only = dialog.get_file_filter() == 0;
    m_listing = m_controller.list_directory(m_file_dir, images_only);

    auto labels = std::make_shared<slint::VectorModel<slint::SharedString>>();
    auto paths = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const std::filesystem::path& entry : m_listing) {
        std::string name = entry.filename().string();
        if (std::filesystem::is_directory(entry)) name += "/";
        labels->push_back(slint::SharedString(name));
        paths->push_back(slint::SharedString(entry.string()));
    }
    dialog.set_file_labels(labels);
    dialog.set_file_paths(paths);
}

void UiState::do_file_up() {
    if (!m_file_dir.has_parent_path()) return;
    m_file_dir = m_file_dir.parent_path();
    refresh_file_dialog();
}

void UiState::do_file_navigate(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= m_listing.size()) return;

    const std::filesystem::path& entry = m_listing[static_cast<std::size_t>(row)];
    if (std::filesystem::is_directory(entry)) {
        m_file_dir = entry;
        refresh_file_dialog();
        return;
    }
    // A file click only names it: the accept button does the opening, so a
    // misclick cannot replace the document.
    m_window.global<DialogState>().set_file_path(slint::SharedString(entry.filename().string()));
}

void UiState::do_file_filter_changed(int index) {
    auto& dialog = m_window.global<DialogState>();
    if (dialog.get_file_mode() == EFileDialogMode::SAVE) {
        dialog.set_file_format(index);
        std::filesystem::path path(std::string(dialog.get_file_path()));
        if (!path.empty()) {
            path.replace_extension(extension_for(format_from_index(index)));
            dialog.set_file_path(slint::SharedString(path.string()));
        }
    } else {
        dialog.set_file_filter(index);
    }
    refresh_file_dialog();
}

void UiState::do_file_ok() {
    auto& dialog = m_window.global<DialogState>();
    std::filesystem::path path(std::string(dialog.get_file_path()));
    if (path.empty()) return;
    if (path.is_relative()) path = m_file_dir / path;

    const auto report = [&dialog](const std::string& error) {
        dialog.set_file_error(slint::SharedString(error));
        dialog.set_file_has_error(true);
    };

    if (dialog.get_file_mode() == EFileDialogMode::SAVE) {
        const ImageFormat format = format_from_index(dialog.get_file_format());
        if (!path.has_extension()) path += extension_for(format);
        const IoResult result = m_controller.save_document(path, format);
        if (!result.ok) {
            report(result.error);
            return;
        }
        dialog.set_file_visible(false);
        dialog.set_file_has_error(false);
        set_title();
        m_window.invoke_focus_app();
        // A save that was asked for by the unsaved-changes question finishes
        // whatever the user was doing when it appeared.
        run_pending();
        return;
    }

    if (dialog.get_file_mode() == EFileDialogMode::PASTE) {
        const IoResult result = m_controller.paste_from_file(path);
        if (!result.ok) {
            report(result.error);
            return;
        }
        dialog.set_file_visible(false);
        m_window.invoke_focus_app();
        m_window.global<UserState>().set_selected_tool(ETool::SELECTION);
        refresh();
        return;
    }

    const IoResult result = m_controller.open_document(path);
    if (!result.ok) {
        report(result.error);
        return;
    }
    dialog.set_file_visible(false);
    dialog.set_file_has_error(false);
    m_window.invoke_focus_app();
    after_document_change();
}

void UiState::do_file_cancel() {
    m_window.global<DialogState>().set_file_visible(false);
    m_window.invoke_focus_app();
    m_pending = PendingAction::None;
}

void UiState::do_open_resize_dialog() {
    auto& dialog = m_window.global<DialogState>();
    dialog.set_resize_width(slint::SharedString(std::to_string(m_controller.document().width())));
    dialog.set_resize_height(slint::SharedString(std::to_string(m_controller.document().height())));
    dialog.set_resize_visible(true);
}

void UiState::do_resize_ok() {
    auto& dialog = m_window.global<DialogState>();
    const int width = parse_canvas_size(dialog.get_resize_width());
    const int height = parse_canvas_size(dialog.get_resize_height());
    if (width == 0 || height == 0) {
        show_message("Resize image", "Enter a whole number of pixels between 1 and " +
                                          std::to_string(kMaxCanvasDimension) + ".");
        return;
    }
    dialog.set_resize_visible(false);
    m_window.invoke_focus_app();
    m_controller.resize_canvas(width, height,
                               to_rgba(m_window.global<UserState>().get_secondary_color()));
    refresh();
}

void UiState::do_resize_cancel() {
    m_window.global<DialogState>().set_resize_visible(false);
    m_window.invoke_focus_app();
}

}  // namespace lsp
