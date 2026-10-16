#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/raster.hpp"
#include "core/types.hpp"
#include "tools/tool.hpp"

// Shape tool: the whole shape vocabulary of mspaint, its outline geometry, and
// the rasterizer that turns a finished drag into pixels.
//
// `shape::outline()` is the single source of geometry: the ribbon glyphs, the
// drag preview and the committed raster all read the same point list, so what
// the user sees while dragging is what lands on the canvas.
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

// Every kind in ribbon order; the gallery and the tests iterate this.
inline constexpr std::array<ShapeId, 23> kShapeIds{
    ShapeId::Line,      ShapeId::Curve,          ShapeId::Oval,          ShapeId::Rectangle,
    ShapeId::RoundedRect, ShapeId::Polygon,      ShapeId::Triangle,      ShapeId::RightTriangle,
    ShapeId::Diamond,   ShapeId::Pentagon,       ShapeId::Hexagon,       ShapeId::ArrowRight,
    ShapeId::ArrowLeft, ShapeId::ArrowUp,        ShapeId::ArrowDown,     ShapeId::Star4,
    ShapeId::Star5,     ShapeId::Star6,          ShapeId::CalloutRounded, ShapeId::CalloutOval,
    ShapeId::CalloutCloud, ShapeId::Heart,       ShapeId::Lightning,
};

enum class ShapeMode { Outline, Fill, Both };

struct ShapeOptions {
    ShapeId kind = ShapeId::Rectangle;
    ShapeMode mode = ShapeMode::Outline;
    int thickness = 1;  // px, 1..32 from ToolOptions.shape-thickness
    int corner_radius = 8;
};

// Written by ShapeTool, read by UiState when it synchronizes the canvas overlay.
struct ShapePreview {
    bool visible = false;
    bool draw_fill = false;
    bool draw_outline = true;
    Rgba8 outline_color{0, 0, 0, 255};
    Rgba8 fill_color{255, 255, 255, 255};
    std::string commands;  // SVG path data in canvas pixels
};

namespace shape {

// The drag rectangle of the two corners, at least 1x1 so a click still has an
// outline to draw.
[[nodiscard]] RectI normalized_rect(Point2f a, Point2f b);

// Open shapes (line, curve) can only ever be stroked, never filled.
[[nodiscard]] bool is_closed(ShapeId kind);

// Outline of `kind` in canvas pixels. ShapeId::Polygon returns `polygon`
// unchanged; every other kind ignores it.
[[nodiscard]] std::vector<Point2f> outline(ShapeId kind, Point2f a, Point2f b,
                                           std::span<const Point2f> polygon);

[[nodiscard]] std::string svg_commands(std::span<const Point2f> points, bool closed);

// Glyph for the shape gallery, drawn inside `box`.
[[nodiscard]] std::string gallery_commands(ShapeId kind, RectI box);

}  // namespace shape

// Paints `kind` into `target`. `outline_color` draws the border and
// `fill_color` the interior, matching mspaint's Color 1 / Color 2 split.
void rasterize_shape(raster::Target target, ShapeId kind, Point2f a, Point2f b,
                     std::span<const Point2f> polygon, const ShapeOptions& options,
                     Rgba8 outline_color, Rgba8 fill_color);

class ShapeTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;
    void on_key_pressed(ToolContext& context, std::string_view text) override;
    void on_double_click(ToolContext& context) override;
    void on_deactivate(ToolContext& context) override;

private:
    void start_drag(ToolContext& context, Point2f position, PointerButton button);
    void update_preview(ToolContext& context, Point2f cursor);
    void commit_drag(ToolContext& context, Point2f end);
    void close_polygon(ToolContext& context);
    void cancel_polygon(ToolContext& context);

    bool m_dragging = false;
    PointerButton m_button = PointerButton::Primary;
    Point2f m_origin{};
    Point2f m_cursor{};
    std::vector<Point2f> m_polygon;
    bool m_polygon_open = false;
};

}  // namespace lsp
