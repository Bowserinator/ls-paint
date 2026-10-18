#include "ui/ui_state.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "core/color.hpp"
#include "io/clipboard.hpp"
#include "io/document_io.hpp"
#include "store/store_sqlite.hpp"
#include "text/font_catalog.hpp"
#include "tools/selection_tool.hpp"
#include "tools/text_tool.hpp"
#include "ui/cursor_overlay.hpp"
#include "ui/file_picker.hpp"
#include "ui/shortcuts.hpp"
#include "utils/key_text.hpp"
#include "utils/math.hpp"
#include "utils/path_text.hpp"
#include "utils/text.hpp"

namespace lsp {

namespace {

const slint::Color kWhiteColor = slint::Color::from_rgb_uint8(255, 255, 255);

// The view's backdrop around the paper, which the pointer's marks read against
// where the paper ends.
constexpr Rgba8 kCanvasViewColor{0xc5, 0xcf, 0xdf, 255};

slint::Color to_slint(Rgba8 color) {
    return slint::Color::from_argb_uint8(color.a, color.r, color.g, color.b);
}

// Ink is always opaque: a color with alpha 0 would make every stroke invisible
// while the pencil still appears to be drawing.
Rgba8 to_rgba(slint::Color color) {
    return {color.red(), color.green(), color.blue(), 255};
}

// A colour as it is stored: six hex digits, red first.
std::string color_text(const slint::Color& color) { return to_hex(to_rgba(color)).substr(1); }

// The colour a stored string names, or nothing when it is not six hex digits.
std::optional<slint::Color> color_from_text(std::string_view text) {
    if (text.size() != 6) return std::nullopt;
    const std::optional<Rgba8> parsed = parse_color(text);
    if (!parsed) return std::nullopt;
    return to_slint(*parsed);
}

// The canvas size fields hold digits and nothing else, so anything else is a
// size the user has not finished typing.
[[nodiscard]] int parse_canvas_size(const slint::SharedString& text) {
    const std::string_view value(text);
    if (!std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; })) return 0;
    const std::optional<int> size = parse_int(value);
    if (!size || *size < 1 || *size > kMaxCanvasDimension) return 0;
    return *size;
}

// Marching ants: alternating light and dark runs three pixels long, advanced
// one pixel per tick. The whole pattern is rebuilt from the selection's
// border on every tick, so the animation is just the phase moving forward.
constexpr int kAntSegment = 3;
constexpr int kAntPeriod = kAntSegment * 2;
constexpr auto kAntTick = std::chrono::milliseconds(80);

// A second press this soon and this close to the first counts as a double click.
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);
constexpr float kDoubleClickSlop = 4.0f;

// The picker draws every glyph in this box, whatever the size it shows it at.
constexpr int kGlyphBox = 26;

// The order of the shape picker, in rows of six.
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
        const int x0 = round_to_int(points[i - 1].x);
        const int y0 = round_to_int(points[i - 1].y);
        const int x1 = round_to_int(points[i].x);
        const int y1 = round_to_int(points[i].y);
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

std::vector<SelectionGrip> grip_anchors(const RectI& bounds) {
    std::vector<SelectionGrip> grips;
    for (const SelectionHandle handle : kSelectionHandles) {
        const Point2f anchor = handle_anchor(bounds, handle);
        grips.push_back({anchor.x, anchor.y});
    }
    return grips;
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
        case EBrushKind::BLUR: return BrushKind::Blur;
        case EBrushKind::LIQUIFY: return BrushKind::Liquify;
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

// The outline of a turned block: the corners of the box its run started from,
// turned by the angle it has turned since. The mask stays axis aligned, so this
// is the shape the contents actually cover.
std::vector<Point2f> turned_corners(const FloatingContents& contents) {
    const RectI box = contents.turn_bounds;
    const float centre_x = box.x + box.width / 2.0f;
    const float centre_y = box.y + box.height / 2.0f;
    // Snapped so a right angle lands on exact pixels, as the rotation does.
    const auto [straight, square] = turn_by(radians_from_degrees(contents.rotation));
    const float left = static_cast<float>(box.x);
    const float top = static_cast<float>(box.y);
    const float right = static_cast<float>(box.x + box.width);
    const float bottom = static_cast<float>(box.y + box.height);
    const float corners[4][2] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    std::vector<Point2f> points;
    points.reserve(5);
    for (const auto& corner : corners) {
        const float dx = corner[0] - centre_x;
        const float dy = corner[1] - centre_y;
        points.push_back(
            {centre_x + dx * straight - dy * square, centre_y + dx * square + dy * straight});
    }
    points.push_back(points.front());  // closed, so the last edge is drawn too
    return points;
}

}  // namespace

UiState::UiState(AppWindow& window, AppController& controller)
    : m_window(window),
      m_controller(controller),
      m_bridge(controller.document().width(), controller.document().height()) {
    m_window.set_tile_size(kDisplayTileSize);
    m_window.set_tiles(m_bridge.tiles());
    const auto show_view = [this](const ViewArea& area) {
        // A margin of a pixel each way covers the partly visible edge ones.
        const int left = floor_to_int(area.x) - 1;
        const int top = floor_to_int(area.y) - 1;
        m_bridge.set_view(m_controller.document().flattened(),
                          RectI{left, top, ceil_to_int(area.width) + 2, ceil_to_int(area.height) + 2});
    };
    m_window.on_view_area_changed(show_view);
    show_view(m_window.get_view_area());
    m_window.on_zoom_level_changed([this](int level) {
        if (!m_bridge.set_level(level)) return;
        const PixelBuffer& pixels = m_controller.document().flattened();
        m_bridge.upload(pixels, pixels.bounds());
    });

    m_palette = std::make_shared<slint::VectorModel<slint::Color>>();
    for (const Rgba8& color : kDefaultPalette)
        m_palette->push_back(slint::Color::from_rgb_uint8(color.r, color.g, color.b));
    for (int i = 0; i < kCustomColorSlots; ++i)
        m_palette->push_back(kWhiteColor);
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
        m_controller.brush().size = std::max(1, round_to_int(tool_options.get_brush_size()));
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
            distance({x, y}, m_last_press_position) <= kDoubleClickSlop;
        m_have_last_press = true;
        m_last_press = now;
        m_last_press_position = {x, y};

        m_controller.input().pointer_down(tool, {x, y},
                                          secondary ? PointerButton::Secondary : PointerButton::Primary);
        if (double_click) m_controller.input().double_click(tool);

        const ColorPair& colors = m_controller.colors();
        const Rgba8 primary_ink = colors.ink(PointerButton::Primary);
        const Rgba8 secondary_ink = colors.ink(PointerButton::Secondary);
        state.set_primary_color(to_slint(primary_ink));
        state.set_secondary_color(to_slint(secondary_ink));
        refresh();
    });

    m_window.global<UserState>().on_pointer_move([this](float x, float y, bool shift) {
        m_controller.input().pointer_move(
            tool_from_slint(m_window.global<UserState>().get_selected_tool()), {x, y}, shift);
        refresh();
    });

    m_window.global<UserState>().on_pointer_up([this](float x, float y, bool secondary) {
        m_controller.input().pointer_up(
            {x, y}, secondary ? PointerButton::Secondary : PointerButton::Primary);
        refresh();
    });

    m_window.global<UserState>().on_pointer_cancel([this] {
        if (!m_controller.input().active()) return;
        m_controller.input().pointer_cancel();
        refresh();
    });

    m_window.global<UserState>().on_canvas_resize([this](float width, float height) {
        m_controller.resize_canvas(std::clamp(round_to_int(width), 1, kMaxCanvasDimension),
                                   std::clamp(round_to_int(height), 1, kMaxCanvasDimension),
                                   to_rgba(m_window.global<UserState>().get_secondary_color()));
        refresh();
    });

    m_window.global<UserState>().on_key_pressed([this](const slint::SharedString& text, bool ctrl, bool shift, bool alt) {
        if (m_controller.input().active()) {
            // Never act mid-stroke; Escape is the one key that abandons it.
            if (std::string_view(text) == kKeyEscape &&
                m_controller.input().cancel(
                    tool_from_slint(m_window.global<UserState>().get_selected_tool()))) {
                refresh();
            }
            return;
        }
        // While a text box is open every key belongs to it, including the ones
        // that would otherwise be shortcuts.
        if (text_open()) return;
        // A block still hovering is a paste or a lift that never landed, so
        // Escape drops it rather than writing it down.
        if (std::string_view(text) == kKeyEscape && m_controller.discard_floating()) {
            refresh();
            return;
        }
        // Enter and Escape end the active tool's own gesture (a polygon); the
        // shortcut table below only ever sees what that leaves behind.
        m_controller.input().key(
            tool_from_slint(m_window.global<UserState>().get_selected_tool()), std::string_view(text));
        const auto action = lookup_shortcut(std::string_view(text), ctrl, shift, alt);
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
            case ShortcutAction::Delete:
                // Nothing selected means nothing to clear, and unlike the menu
                // command this key never touches the whole canvas. Cut and the
                // eraser erase to the secondary colour, so this does too.
                if (m_controller.document().selection().active()) m_controller.delete_selection_contents();
                break;
            case ShortcutAction::New: do_file_new(); break;
            case ShortcutAction::Open: do_file_open(); break;
            case ShortcutAction::Save: (void)save_document(); break;
            case ShortcutAction::SaveAs: (void)save_document_as(); break;
            case ShortcutAction::ToggleRulers: {
                auto& state = m_window.global<UserState>();
                state.set_show_rulers(!state.get_show_rulers());
                break;
            }
            case ShortcutAction::ToggleGrid: {
                auto& state = m_window.global<UserState>();
                state.set_show_gridlines(!state.get_show_gridlines());
                break;
            }
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
    m_window.global<UserState>().on_check_clipboard([this] { check_clipboard(); });
    m_window.global<UserState>().on_transform_selection_content(
        [this](EContentTransform transform) {
            sync_colors();
            m_controller.transform_selection_contents(transform_from_slint(transform));
            refresh();
        });

    m_window.global<UserState>().on_file_new([this] { do_file_new(); });
    m_window.global<UserState>().on_file_open([this] { do_file_open(); });
    m_window.global<UserState>().on_file_save([this] { (void)save_document(); });
    m_window.global<UserState>().on_file_save_as([this] { (void)save_document_as(); });
    m_window.global<UserState>().on_file_exit([this] { do_file_exit(); });
    m_window.global<UserState>().on_file_open_recent([this](int index) { open_recent(index); });
    m_window.global<UserState>().on_paste_from_file([this] { do_paste_from_file(); });

    m_window.global<UserState>().on_resize_dialog([this] { do_open_resize_dialog(); });
    m_window.global<UserState>().on_edit_colors([this] { do_open_color_dialog(); });
    m_window.global<UserState>().on_about([this] { do_open_about(); });

    // The window's close button asks the same question as File > Exit.
    m_window.window().on_close_requested([this] {
        save_settings();
        if (!m_controller.history().is_modified()) return slint::CloseRequestResponse::HideWindow;
        do_file_exit();
        return slint::CloseRequestResponse::KeepWindowShown;
    });

    check_clipboard();
    refresh();

    // The outline crawls on its own: nothing else has to touch it.
    m_ants_timer.start(slint::TimerMode::Repeated, kAntTick, [this] {
        m_ant_phase = (m_ant_phase + 1) % kAntPeriod;
        update_ants();
    });

    // The settings of the previous run, and the timer that notices a move or
    // a resize of the window, which Slint never reports.
    m_settings = std::make_unique<SQLiteStore>(default_store_path());
    load_settings();
    m_settings_timer.start(slint::TimerMode::Repeated, std::chrono::seconds(1),
                           [this] { watch_settings(); });
}

void UiState::load_settings() {
    if (!m_settings) return;

    if (const std::optional<std::string> family = m_settings->get(kStoreKeyFontFamily);
        family && !family->empty()) {
        m_window.global<FontStyle>().set_family(slint::SharedString(*family));
        // The preview is drawn by Slint, which keeps the font size, while the
        // commit is rasterized to the metrics of the face.
        m_window.global<FontStyle>().set_preview_scale(
            text_preview_scale(std::string_view(*family)));
    }
    if (const std::optional<bool> stored = stored_bool(*m_settings, kStoreKeyShowRulers))
        m_window.global<UserState>().set_show_rulers(*stored);
    if (const std::optional<bool> stored = stored_bool(*m_settings, kStoreKeyShowGridlines))
        m_window.global<UserState>().set_show_gridlines(*stored);
    if (const std::optional<bool> stored = stored_bool(*m_settings, kStoreKeyShowStatusBar))
        m_window.global<UserState>().set_show_status_bar(*stored);

    restore_settings();

    // The File menu's recent list, newest entry first.
    if (const std::optional<std::string> stored = m_settings->get(kStoreKeyRecentFiles)) {
        for (const std::string& entry : decode_list(*stored)) {
            if (entry.empty()) continue;
            if (m_recent_files.size() >= kMaxRecentFiles) break;
            m_recent_files.push_back(path_from_utf8(entry));
        }
    }
    publish_recent_files();

    // The custom colours the user has mixed, in the order the two grids hold
    // them.
    if (m_palette) {
        if (const std::optional<std::string> stored = m_settings->get(kStoreKeyPaletteCustoms)) {
            const std::vector<std::string> entries = decode_list(*stored);
            for (std::size_t i = 0; i < entries.size() && i < kCustomColorSlots; ++i) {
                if (const std::optional<slint::Color> color = color_from_text(entries[i]))
                    m_palette->set_row_data(kDefaultPalette.size() + i, *color);
            }
        }
    }
    if (const std::optional<std::string> stored = m_settings->get(kStoreKeyDialogCustoms)) {
        for (const std::string& entry : decode_list(*stored)) {
            if (m_dialog_customs_stored.size() >= kDialogCustomColorSlots) break;
            if (const std::optional<slint::Color> color = color_from_text(entry))
                m_dialog_customs_stored.push_back(*color);
        }
    }
    if (const std::optional<std::string> stored = m_settings->get(kStoreKeyLastDirectory))
        m_last_directory = path_from_utf8(*stored);

    // The text tool draws with the family the box is set in.
    sync_font_style();
    m_stored_settings = current_settings();
}

UiState::SettingsSnapshot UiState::current_settings() const {
    SettingsSnapshot snapshot;
    const slint::Window& window = m_window.window();
    const float scale = window.scale_factor() > 0.0f ? window.scale_factor() : 1.0f;
    const slint::PhysicalPosition position = window.position();
    const slint::PhysicalSize size = window.size();
    const auto to_logical = [scale](int value) {
        return round_to_int(static_cast<float>(value) / scale);
    };
    snapshot.x = to_logical(position.x);
    snapshot.y = to_logical(position.y);
    snapshot.width = to_logical(size.width);
    snapshot.height = to_logical(size.height);
    const UserState& user = m_window.global<UserState>();
    snapshot.rulers = user.get_show_rulers();
    snapshot.gridlines = user.get_show_gridlines();
    snapshot.status_bar = user.get_show_status_bar();
    snapshot.family = std::string(m_window.global<FontStyle>().get_family());
    return snapshot;
}

void UiState::restore_settings() {
    if (!m_settings) return;
    slint::Window& window = m_window.window();
    // The geometry is stored in logical pixels, so the same numbers mean the
    // same window on a screen with a different scale factor.
    const std::optional<int> width = stored_int(*m_settings, kStoreKeyWindowWidth);
    const std::optional<int> height = stored_int(*m_settings, kStoreKeyWindowHeight);
    if (width && height && *width > 0 && *height > 0) {
        window.set_size(slint::LogicalSize({static_cast<float>(*width), static_cast<float>(*height)}));
    }
    const std::optional<int> x = stored_int(*m_settings, kStoreKeyWindowX);
    const std::optional<int> y = stored_int(*m_settings, kStoreKeyWindowY);
    if (x && y) window.set_position(slint::LogicalPosition({static_cast<float>(*x), static_cast<float>(*y)}));
}

void UiState::save_settings() {
    if (!m_settings) return;
    const SettingsSnapshot snapshot = current_settings();
    m_settings->set(kStoreKeyWindowX, std::to_string(snapshot.x));
    m_settings->set(kStoreKeyWindowY, std::to_string(snapshot.y));
    m_settings->set(kStoreKeyWindowWidth, std::to_string(snapshot.width));
    m_settings->set(kStoreKeyWindowHeight, std::to_string(snapshot.height));
    m_settings->set(kStoreKeyShowRulers, snapshot.rulers ? "1" : "0");
    m_settings->set(kStoreKeyShowGridlines, snapshot.gridlines ? "1" : "0");
    m_settings->set(kStoreKeyShowStatusBar, snapshot.status_bar ? "1" : "0");
    m_settings->set(kStoreKeyFontFamily, snapshot.family);
    std::vector<std::string> recent;
    recent.reserve(m_recent_files.size());
    for (const std::filesystem::path& path : m_recent_files) recent.push_back(utf8_from_path(path));
    m_settings->set(kStoreKeyRecentFiles, encode_list(recent));
    std::vector<std::string> customs;
    if (m_palette) {
        for (std::size_t i = 0; i < kCustomColorSlots; ++i) {
            const std::optional<slint::Color> color = m_palette->row_data(kDefaultPalette.size() + i);
            customs.push_back(color_text(color.value_or(kWhiteColor)));
        }
    }
    m_settings->set(kStoreKeyPaletteCustoms, encode_list(customs));
    std::vector<std::string> dialog_customs;
    if (m_dialog_customs) {
        for (int i = 0; i < kDialogCustomColorSlots; ++i) {
            const std::optional<slint::Color> color =
                m_dialog_customs->row_data(static_cast<std::size_t>(i));
            dialog_customs.push_back(color_text(color.value_or(kWhiteColor)));
        }
    } else {
        for (const slint::Color& color : m_dialog_customs_stored)
            dialog_customs.push_back(color_text(color));
    }
    m_settings->set(kStoreKeyDialogCustoms, encode_list(dialog_customs));
    m_settings->set(kStoreKeyLastDirectory, utf8_from_path(m_last_directory));
    m_settings->save();
    m_stored_settings = snapshot;
    m_colors_dirty = false;
}

void UiState::watch_settings() {
    if (!m_settings) return;
    if (current_settings() != m_stored_settings || m_colors_dirty) save_settings();
}

void UiState::set_palette_color(int index, const slint::Color& color) {
    if (!m_palette || index < 0 || index >= static_cast<int>(m_palette->row_count())) return;
    m_palette->set_row_data(static_cast<std::size_t>(index), color);
    if (index >= static_cast<int>(kDefaultPalette.size())) m_colors_dirty = true;
}

// The dialog starts with the ribbon's strip in its grid and empty slots after
// it, and is free to change them from then on.
void UiState::seed_dialog_customs() {
    lsp_colors::EditColorsWindow* picker = color_picker();
    if (!picker) return;
    m_dialog_customs = std::make_shared<slint::VectorModel<slint::Color>>();
    for (int i = 0; i < kDialogCustomColorSlots; ++i) {
        slint::Color color = kWhiteColor;
        if (i < static_cast<int>(m_dialog_customs_stored.size())) {
            color = m_dialog_customs_stored[static_cast<std::size_t>(i)];
        } else if (i < kCustomColorSlots && m_palette) {
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
    if (static_cast<std::size_t>(index) < m_dialog_customs_stored.size())
        m_dialog_customs_stored[static_cast<std::size_t>(index)] = color;
    m_colors_dirty = true;
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
        kept.push_back(stored.value_or(kWhiteColor));
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
    window.on_cancelled([this] { close_color_window(); });
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
    picker->set_current(to_slint(color));
    picker->set_hue_color(to_slint(hue_color));
    picker->set_red_text(slint::SharedString(std::to_string(color.r)));
    picker->set_green_text(slint::SharedString(std::to_string(color.g)));
    picker->set_blue_text(slint::SharedString(std::to_string(color.b)));
    // RRGGBB in capitals, with no leading hash: the box is labelled Hex.
    picker->set_hex_text(slint::SharedString(uppered(to_hex(color).substr(1))));
    picker->set_hue_text(slint::SharedString(std::to_string(round_to_int(hsv.hue))));
    picker->set_sat_text(slint::SharedString(std::to_string(round_to_int(hsv.saturation * 100.0f))));
    picker->set_lum_text(slint::SharedString(std::to_string(round_to_int(hsv.value * 100.0f))));
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
    close_color_window();
    sync_colors();
    refresh();
}

void UiState::close_color_window() {
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
    color_picker()->set_custom_index(std::clamp(index, 0, kDialogCustomColorSlots - 1));
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
    picker->set_hue(wrap_hue(degrees_from_radians(std::atan2(dx, -dy))));
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

void UiState::ensure_confirm_window() {
    if (m_confirm_window) return;
    m_confirm_window = lsp_confirm::ConfirmDialog::create();
    lsp_confirm::ConfirmDialog& dialog = **m_confirm_window;
    dialog.on_accepted([this] {
        close_confirm_window();
        // The pending action waits for the save, and is dropped when the user
        // backs out of it or it fails.
        if (!save_document()) {
            m_pending = PendingAction::None;
            return;
        }
        run_pending();
    });
    dialog.on_discarded([this] {
        close_confirm_window();
        run_pending();
    });
    dialog.on_cancelled([this] {
        close_confirm_window();
        m_pending = PendingAction::None;
    });
    dialog.window().on_close_requested([this] {
        // Closing the question with the window manager drops whatever waited on
        // the answer.
        m_pending = PendingAction::None;
        m_window.invoke_focus_app();
        return slint::CloseRequestResponse::HideWindow;
    });
}

void UiState::close_confirm_window() {
    if (m_confirm_window) (*m_confirm_window)->hide();
    m_window.invoke_focus_app();
}

lsp_confirm::ConfirmDialog& UiState::confirm_window() {
    ensure_confirm_window();
    return **m_confirm_window;
}

bool UiState::confirm_window_open() const noexcept {
    return m_confirm_window && (*m_confirm_window)->window().is_visible();
}

void UiState::ensure_message_window() {
    if (m_message_window) return;
    m_message_window = lsp_message::MessageDialog::create();
    (*m_message_window)->on_accepted([this] { close_message_window(); });
}

void UiState::close_message_window() {
    if (m_message_window) (*m_message_window)->hide();
    m_window.invoke_focus_app();
}

lsp_message::MessageDialog& UiState::message_window() {
    ensure_message_window();
    return **m_message_window;
}

bool UiState::message_window_open() const noexcept {
    return m_message_window && (*m_message_window)->window().is_visible();
}

void UiState::ensure_resize_window() {
    if (m_resize_window) return;
    m_resize_window = lsp_resize::ResizeDialog::create();
    lsp_resize::ResizeDialog& dialog = **m_resize_window;
    dialog.on_accepted([this] { do_resize_ok(); });
    dialog.on_cancelled([this] { close_resize_window(); });
    dialog.window().on_close_requested([this] {
        // Closing the question with the window manager is the same as
        // cancelling it.
        close_resize_window();
        return slint::CloseRequestResponse::HideWindow;
    });
}

void UiState::close_resize_window() {
    if (m_resize_window) (*m_resize_window)->hide();
    m_window.invoke_focus_app();
}

lsp_resize::ResizeDialog& UiState::resize_window() {
    ensure_resize_window();
    return **m_resize_window;
}

bool UiState::resize_window_open() const noexcept {
    return m_resize_window && (*m_resize_window)->window().is_visible();
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
    const int x = floor_to_int(state.get_cursor_x());
    const int y = floor_to_int(state.get_cursor_y());
    if (!state.get_cursor_on_canvas() || !paper.contains(x, y)) {
        state.set_hover_color_text(slint::SharedString());
        return;
    }

    const Rgba8 pixel = paper.at(x, y);
    // The readout is upper case, the way Paint prints a colour.
    state.set_hover_color_text(
        slint::SharedString("(" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," +
                            std::to_string(pixel.b) + ") " + uppered(to_hex(pixel))));
}

void UiState::publish_cursor_overlay() {
    auto& state = m_window.global<UserState>();
    if (!state.get_cursor_on_canvas()) {
        state.set_cursor_overlay(slint::Image());
        state.set_cursor_overlay_reach(0.0F);
        return;
    }

    const auto& options = m_window.global<ToolOptions>();
    const ETool tool = state.get_selected_tool();
    // Only the tools that paint a stroke have a tip to show.
    const bool brush_tool =
        tool == ETool::BRUSH || tool == ETool::PENCIL || tool == ETool::ERASER;
    const EBrushKind kind = tool == ETool::BRUSH ? options.get_brush_kind() : EBrushKind::BRUSH;
    const float zoom = state.get_zoom();
    const int size = std::max(1, round_to_int(options.get_brush_size()));
    // The tip is the brush size at the view's zoom, across in screen pixels.
    const float length = static_cast<float>(size) * zoom;
    // The engine stamps an odd tip on a pixel and an even one on a pixel corner.
    const float tip_x = size % 2 == 1 ? std::floor(state.get_cursor_x()) + 0.5F
                                      : std::round(state.get_cursor_x());
    const float tip_y = size % 2 == 1 ? std::floor(state.get_cursor_y()) + 0.5F
                                      : std::round(state.get_cursor_y());
    const bool nib = kind == EBrushKind::CALLIGRAPHY1 || kind == EBrushKind::CALLIGRAPHY2;

    CursorShape shape;
    shape.pointer = {state.get_cursor_x(), state.get_cursor_y()};
    shape.zoom = zoom;
    shape.tip.shown = brush_tool;
    shape.tip.square =
        kind == EBrushKind::MARKER || (kind == EBrushKind::BRUSH && options.get_brush_square());
    shape.tip.half_width = length / 2.0F;
    shape.tip.half_height = (nib ? std::max(2.0F, length * 0.3F) : length) / 2.0F;
    shape.tip.offset_x = (tip_x - shape.pointer.x) * zoom;
    shape.tip.offset_y = (tip_y - shape.pointer.y) * zoom;
    shape.tip.rotation_degrees = kind == EBrushKind::CALLIGRAPHY1   ? 45.0F
                                 : kind == EBrushKind::CALLIGRAPHY2 ? -45.0F
                                                                    : 0.0F;
    // Below sixteen screen pixels across, the tip is too small to find by its
    // outline alone.
    shape.crosshair = !brush_tool || length < 16.0F;

    const CursorOverlay overlay =
        render_cursor_overlay(m_controller.document().flattened(), shape, kCanvasViewColor);
    state.set_cursor_overlay(overlay.reach > 0 ? image_from(overlay.pixels) : slint::Image());
    state.set_cursor_overlay_reach(static_cast<float>(overlay.reach));
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
    }

    auto& state = m_window.global<DocumentState>();
    state.set_canvas_width(document.width());
    state.set_canvas_height(document.height());

    const SelectionMask& selection = document.selection();
    const RectI bounds = selection.bounds();
    state.set_has_selection(selection.active() && !bounds.empty());
    state.set_has_clipboard(!m_controller.clipboard().empty() || m_system_clipboard);
    state.set_selection_x(bounds.x);
    state.set_selection_y(bounds.y);
    state.set_selection_width(bounds.width);
    state.set_selection_height(bounds.height);

    publish_hover_color();
    publish_cursor_overlay();

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
    overlay.set_outline_color(to_slint(preview.outline_color));
    overlay.set_fill_color(to_slint(preview.fill_color));

    const TextOverlayState& text = m_controller.tool_ui().text;
    auto& text_box = m_window.global<TextOverlay>();
    text_box.set_visible(text.visible);
    // A box that has just opened starts empty; after that the editor owns it.
    if (text.visible && !m_text_was_visible) text_box.set_content(slint::SharedString());
    m_text_was_visible = text.visible;
    text_box.set_x(text.origin.x);
    text_box.set_y(text.origin.y);
    text_box.set_color(to_slint(text.color));
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
    if (visible) {
        // A turned float is not the box its mask holds, so its outline follows
        // the shape its contents cover.
        const std::optional<FloatingContents>& floating = document.floating();
        if (floating.has_value() && floating->rotation != 0.0f) {
            append_path_runs(turned_corners(*floating), m_ant_phase, dark, light);
        } else {
            append_border_runs(bounds, m_ant_phase, dark, light);
        }
    }
    if (drawing) append_path_runs(preview.points, m_ant_phase, dark, light);
    state.set_ants_dark(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(dark)));
    state.set_ants_light(std::make_shared<slint::VectorModel<SelectionDash>>(std::move(light)));

    std::vector<SelectionGrip> grips;
    if (visible && bounds.width >= kMinHandleSpan && bounds.height >= kMinHandleSpan &&
        m_window.global<UserState>().get_selected_tool() == ETool::SELECTION) {
        grips = grip_anchors(bounds);
    }
    state.set_grips(std::make_shared<slint::VectorModel<SelectionGrip>>(std::move(grips)));

    // The rotate badge. Written only when it changes: this runs on a timer, and
    // fresh values every tick would repaint the window forever.
    const RotatePreview& rotate = m_controller.tool_ui().rotate;
    if (rotate.hover != m_rotate_hover || rotate.rotating != m_rotate_active) {
        m_rotate_hover = rotate.hover;
        m_rotate_active = rotate.rotating;
        auto& zone = m_window.global<RotateOverlay>();
        zone.set_hover(rotate.hover);
        zone.set_rotating(rotate.rotating);
    }
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
    refresh();
}

void UiState::ask_unsaved(PendingAction action) {
    m_pending = action;
    ensure_confirm_window();
    (*m_confirm_window)
        ->set_message(slint::SharedString("Do you want to save changes to " +
                                          m_controller.display_name() + "?"));
    (*m_confirm_window)->show();
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
        case PendingAction::Open: open_document(); break;
        case PendingAction::OpenRecent: {
            const std::filesystem::path path = m_pending_path;
            m_pending_path.clear();
            (void)open_path(path);
            break;
        }
        case PendingAction::Exit:
            save_settings();
            m_window.hide();
            break;
    }
}

// Adds a path to the File menu's recent list: newest first, each path once,
// and never more than the list holds.
void UiState::note_recent_file(const std::filesystem::path& path) {
    if (path.empty()) return;
    const std::filesystem::path ready = std::filesystem::absolute(path).lexically_normal();
    std::erase(m_recent_files, ready);
    m_recent_files.insert(m_recent_files.begin(), ready);
    if (m_recent_files.size() > kMaxRecentFiles) m_recent_files.resize(kMaxRecentFiles);
    const std::filesystem::path folder = ready.parent_path();
    if (!folder.empty()) m_last_directory = folder;
    publish_recent_files();
    save_settings();
}

void UiState::forget_recent_file(const std::filesystem::path& path) {
    if (std::erase(m_recent_files, path) == 0) return;
    publish_recent_files();
    save_settings();
}

void UiState::publish_recent_files() {
    auto model = std::make_shared<slint::VectorModel<slint::SharedString>>();
    for (const std::filesystem::path& path : m_recent_files)
        model->push_back(slint::SharedString(utf8_from_path(path)));
    m_window.global<UserState>().set_recent_files(model);
}

void UiState::open_recent(int index) {
    if (index < 0 || index >= static_cast<int>(m_recent_files.size())) return;
    m_pending_path = m_recent_files[static_cast<std::size_t>(index)];
    if (!std::filesystem::exists(m_pending_path)) {
        // A file that is no longer there leaves the list without a word.
        const std::filesystem::path missing = m_pending_path;
        m_pending_path.clear();
        forget_recent_file(missing);
        return;
    }
    if (m_controller.history().is_modified()) {
        ask_unsaved(PendingAction::OpenRecent);
        return;
    }
    m_pending = PendingAction::OpenRecent;
    run_pending();
}

bool UiState::open_startup_file(const std::filesystem::path& path) {
    std::error_code error;
    return open_path(std::filesystem::absolute(path, error));
}

bool UiState::open_path(const std::filesystem::path& path) {
    if (const IoResult result = m_controller.open_document(path); !result.ok) {
        show_message("Open failed", result.error);
        return false;
    }
    after_document_change();
    note_recent_file(path);
    return true;
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
    open_document();
}

bool UiState::save_document() {
    if (!m_controller.has_current_path()) return save_document_as();
    if (const IoResult result = m_controller.save_document(); !result.ok) {
        show_message("Save failed", result.error);
        return false;
    }
    set_title();
    note_recent_file(m_controller.current_path());
    return true;
}

// Where a file dialog opens: the folder the last dialog used, or beside the
// document when no dialog has run yet.
std::filesystem::path UiState::dialog_directory() const {
    if (!m_last_directory.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(m_last_directory, ec))
            return m_last_directory;
    }
    return m_controller.default_directory();
}

bool UiState::save_document_as() {
    if (!ensure_file_dialogs()) return false;
    const std::string stem = m_controller.has_current_path() ? utf8_from_path(m_controller.current_path().stem())
                                                              : std::string("untitled");
    const std::optional<std::filesystem::path> chosen = pick_image_to_save(
        dialog_directory(), stem + extension_for(ImageFormat::Png));
    if (!chosen) return false;

    // A name without a known extension is written as a PNG.
    std::filesystem::path path = *chosen;
    std::optional<ImageFormat> format = format_from_extension(path);
    if (!format) {
        format = ImageFormat::Png;
        path += extension_for(*format);
    }
    if (const IoResult result = m_controller.save_document(path, *format); !result.ok) {
        show_message("Save failed", result.error);
        return false;
    }
    set_title();
    note_recent_file(path);
    return true;
}

void UiState::open_document() {
    if (!ensure_file_dialogs()) return;
    const std::optional<std::filesystem::path> path =
        pick_image_to_open("Open", dialog_directory());
    if (!path) return;
    (void)open_path(*path);
}

bool UiState::ensure_file_dialogs() {
    if (file_dialogs_available()) return true;
    show_message("No file dialog", "This system has no file dialog to show. Install zenity or kdialog.");
    return false;
}

void UiState::do_file_exit() {
    if (m_controller.history().is_modified()) {
        ask_unsaved(PendingAction::Exit);
        return;
    }
    save_settings();
    m_window.hide();
}

// A shell round trip, so it runs when the ribbon changes tab rather than on
// every refresh: that is what keeps the Paste commands honest about what the
// system clipboard holds.
void UiState::check_clipboard() { m_system_clipboard = Clipboard::has_image(); }

void UiState::do_paste() {
    // The system clipboard comes first when it holds an image this program did
    // not put there. A block copied here keeps its mask, which the clipboard
    // cannot carry, so that one is pasted from the copy still in memory.
    std::string error;
    std::optional<PixelBuffer> held = Clipboard::get_image(&error);
    const bool copied_here = held.has_value() && !m_controller.clipboard().empty() &&
                             *held == m_controller.clipboard().pixels;
    if (!held.has_value() || copied_here) {
        if (!m_controller.clipboard().empty()) {
            m_controller.paste_clipboard();
        } else if (!error.empty()) {
            show_message("Paste failed", error);
            return;
        } else {
            std::fprintf(stderr, "Paste failed: the clipboard does not contain an image\n");
            return;
        }
    } else if (const IoResult result = m_controller.paste_image(std::move(*held)); !result.ok) {
        show_message("Paste failed", result.error);
        return;
    }
    m_system_clipboard = true;
    // A pasted block stays a floating selection until it is applied, so hand it
    // to the selection tool: that is what gives it its grips.
    m_window.global<UserState>().set_selected_tool(ETool::SELECTION);
    refresh();
}

void UiState::do_paste_from_file() {
    if (!ensure_file_dialogs()) return;
    const std::optional<std::filesystem::path> path =
        pick_image_to_open("Paste from file", dialog_directory());
    if (!path) return;
    if (const IoResult result = m_controller.paste_from_file(*path); !result.ok) {
        show_message("Paste failed", result.error);
        return;
    }
    m_window.global<UserState>().set_selected_tool(ETool::SELECTION);
    refresh();
}

void UiState::show_message(const std::string& title, const std::string& text) {
    ensure_message_window();
    (*m_message_window)->set_heading(slint::SharedString(title));
    (*m_message_window)->set_text(slint::SharedString(text));
    (*m_message_window)->show();
}

void UiState::do_open_resize_dialog() {
    ensure_resize_window();
    (*m_resize_window)->set_width_text(
        slint::SharedString(std::to_string(m_controller.document().width())));
    (*m_resize_window)->set_height_text(
        slint::SharedString(std::to_string(m_controller.document().height())));
    (*m_resize_window)->show();
}

void UiState::do_resize_ok() {
    lsp_resize::ResizeDialog& dialog = resize_window();
    const int width = parse_canvas_size(dialog.get_width_text());
    const int height = parse_canvas_size(dialog.get_height_text());
    if (width == 0 || height == 0) {
        show_message("Resize image", "Enter a whole number of pixels between 1 and " +
                                          std::to_string(kMaxCanvasDimension) + ".");
        return;
    }
    close_resize_window();
    m_controller.resize_canvas(width, height,
                               to_rgba(m_window.global<UserState>().get_secondary_color()));
    refresh();
}

}  // namespace lsp
