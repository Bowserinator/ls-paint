#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/raster.hpp"
#include "core/types.hpp"
#include "tools/selection_handles.hpp"
#include "tools/tool.hpp"

// Shape tool: the whole shape vocabulary of mspaint, its outline geometry, and
// the rasterizer that turns a finished drag into pixels.
//
// `shape::outline()` is the single source of geometry: the ribbon glyphs and
// the committed raster read the same point list, and the drag preview is
// painted by the committed raster itself, so what the user sees while dragging
// is what lands on the canvas.
namespace lsp {

struct ToolContext;

enum class ShapeId {
    Line,
    Curve,
    Oval,
    Rectangle,
    RoundedRect,
    Polygon,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    ArrowRight,
    ArrowLeft,
    ArrowUp,
    ArrowDown,
    Star4,
    Star5,
    Star6,
    CalloutRounded,
    CalloutOval,
    CalloutCloud,
    Heart,
    Lightning,
};

// How the outline or the fill of a shape is painted. `None` leaves that part
// out, `Solid` paints it in one color, the rest lay down a brush texture.
enum class ShapeTexture { None, Solid, Crayon, Marker, Oil, NaturalPencil, Watercolor };

inline constexpr int kMinShapeThickness = 1;
inline constexpr int kMaxShapeThickness = 32;
inline constexpr int kDefaultCornerRadius = 8;

struct ShapeOptions {
    ShapeId kind = ShapeId::Rectangle;
    ShapeTexture outline = ShapeTexture::Solid;
    ShapeTexture fill = ShapeTexture::None;
    int thickness = kMinShapeThickness;  // px
    int corner_radius = kDefaultCornerRadius;
};

// What the shape tool shows while a gesture is running.
struct ShapePreview {
    bool visible = false;
    // The shape as the commit will paint it, on a transparent sheet, and where
    // the sheet's top left sits in the document.
    PixelBuffer pixels;
    RectI region;
    // Differs whenever the pixels do.
    std::uint64_t revision = 0;
    // What the shape can be grabbed by while it is still editable: the box that
    // carries its resize grips, and the pixels of the points (the ends of a
    // line, the ends and bend of a curve) that are grabbed one by one.
    std::optional<RectI> frame;
    std::vector<Point2f> points;
    // How far the frame, and the shape in it, is turned about its middle, in
    // degrees clockwise.
    float rotation = 0.0f;
};

namespace shape {

// The drag rectangle of the two corners, at least 1x1 so a click still has an
// outline to draw.
[[nodiscard]] RectI normalized_rect(Point2f a, Point2f b);

// Open shapes (line, curve) can only ever be stroked, never filled.
[[nodiscard]] bool is_closed(ShapeId kind);

// Outline of `kind` in canvas pixels. ShapeId::Polygon returns `polygon`
// unchanged and ShapeId::Curve bends towards its single point, if it has one;
// every other kind ignores it.
[[nodiscard]] std::vector<Point2f> outline(ShapeId kind, Point2f a, Point2f b,
                                           std::span<const Point2f> polygon);

// `point` turned clockwise by `degrees` about the middle of `box`'s pixels.
[[nodiscard]] Point2f turned_about_middle(RectI box, Point2f point, float degrees);

[[nodiscard]] std::string svg_commands(std::span<const Point2f> points, bool closed);

// Glyph for the shape gallery, drawn inside `box`.
[[nodiscard]] std::string gallery_commands(ShapeId kind, RectI box);

}  // namespace shape

// Paints `kind` into `target`. `outline_color` draws the border and
// `fill_color` the interior, matching mspaint's Color 1 / Color 2 split. A
// closed shape can be turned by `rotation_degrees` about the middle of its box.
void rasterize_shape(raster::Target target, ShapeId kind, Point2f a, Point2f b,
                     std::span<const Point2f> polygon, const ShapeOptions& options,
                     Rgba8 outline_color, Rgba8 fill_color, float rotation_degrees = 0.0f);

// The options and the two colors a shape is painted with.
struct ShapePaint {
    ShapeOptions options;
    Rgba8 outline;
    Rgba8 fill;
};

class ShapeTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;
    void on_key_pressed(ToolContext& context, std::string_view text) override;
    void on_double_click(ToolContext& context) override;
    bool on_cancel(ToolContext& context) override;
    void on_deactivate(ToolContext& context) override;

private:
    // What a drag on a pending shape is moving.
    enum class Edit { None, Box, Endpoint, Move, Rotate };

    [[nodiscard]] Point2f constrained_end(const ToolContext& context, Point2f end) const;
    void start_drag(ToolContext& context, Point2f position, PointerButton button);
    void update_preview(ToolContext& context, Point2f cursor);
    void finish_drag(ToolContext& context, Point2f end);
    void update_curve_preview(ToolContext& context);
    void commit_curve(ToolContext& context);
    void cancel_curve(ToolContext& context);
    void close_polygon(ToolContext& context);
    void cancel_polygon(ToolContext& context);

    // A finished drag stays unpainted so it can still be resized. The next
    // press elsewhere, Enter or leaving the tool paints it; Escape drops it.
    void publish_pending(ToolContext& context);
    void commit_pending(ToolContext& context);
    void cancel_pending(ToolContext& context);
    [[nodiscard]] bool grab_pending(ToolContext& context, Point2f position);
    void edit_pending(ToolContext& context, Point2f position);
    void hover_pending(ToolContext& context, Point2f position);
    // What a press at `position` on the pending shape would take hold of.
    struct Grab {
        Edit edit = Edit::None;
        std::optional<SelectionHandle> handle;
        int endpoint = 0;
    };
    [[nodiscard]] Grab grab_at(const ToolContext& context, Point2f position) const;
    void hide_preview(ToolContext& context);

    bool m_dragging = false;
    ShapeId m_kind = ShapeId::Rectangle;
    ShapePaint m_paint;
    Point2f m_origin{};
    Point2f m_end{};
    Point2f m_cursor{};
    std::vector<Point2f> m_polygon;
    bool m_polygon_open = false;
    bool m_curve_pending = false;
    bool m_bending = false;
    Point2f m_curve_end{};
    Point2f m_curve_control{};

    bool m_pending = false;
    Edit m_edit = Edit::None;
    SelectionHandle m_handle = SelectionHandle::Nw;
    RectI m_box_before;
    int m_endpoint = 0;
    float m_rotation = 0.0f;
    // The shape as it was when the drag on it began.
    Point2f m_press{};
    Point2f m_origin_before{};
    Point2f m_end_before{};
    float m_rotation_before = 0.0f;
    float m_bearing_before = 0.0f;
};

}  // namespace lsp
