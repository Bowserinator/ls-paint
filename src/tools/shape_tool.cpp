#include "tools/shape_tool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "brushes/brush_engine.hpp"
#include "selection/selection_mask.hpp"
#include "tools/stroke_recorder.hpp"
#include "tools/tool_context.hpp"

namespace lsp {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadians = kPi / 180.0f;

// mspaint's fixed corner for the rounded rectangle and callout kinds.
constexpr float kCornerRadius = 8.0f;
// The callout tail hangs below the body inside the same drag rectangle.
constexpr float kTailHeightRatio = 0.25f;
constexpr float kTailTipRatio = 0.12f;
constexpr float kTailLeftRatio = 0.20f;
constexpr float kTailRightRatio = 0.44f;

Point2f point_at(Point2f center, float rx, float ry, float degrees) {
    return {center.x + rx * std::cos(degrees * kRadians),
            center.y + ry * std::sin(degrees * kRadians)};
}

// Both arc ends are included; a duplicated point where two arcs meet is
// invisible to strokes and fills.
void append_arc(std::vector<Point2f>& out, Point2f center, float rx, float ry, float from_degrees,
                float to_degrees) {
    const float span = std::abs(to_degrees - from_degrees);
    const int samples = std::max(2, static_cast<int>(span / 6.0f));
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        out.push_back(point_at(center, rx, ry, from_degrees + (to_degrees - from_degrees) * t));
    }
}

Point2f corner_of(RectI box, ShapeId kind, Point2f unit) {
    const float x0 = static_cast<float>(box.x);
    const float y0 = static_cast<float>(box.y);
    const float width = static_cast<float>(box.width - 1);
    const float height = static_cast<float>(box.height - 1);
    switch (kind) {
        case ShapeId::ArrowLeft: return {x0 + (1.0f - unit.x) * width, y0 + unit.y * height};
        case ShapeId::ArrowUp: return {x0 + unit.y * width, y0 + (1.0f - unit.x) * height};
        case ShapeId::ArrowDown: return {x0 + unit.y * width, y0 + unit.x * height};
        default: return {x0 + unit.x * width, y0 + unit.y * height};
    }
}

std::vector<Point2f> arrow_points(RectI box, ShapeId kind) {
    // Shaft along the middle, head a fixed share of the arrow's length.
    constexpr float kShaftHalf = 0.28f;
    constexpr float kHeadLength = 0.40f;
    const std::array<Point2f, 7> unit{Point2f{0.0f, 0.5f - kShaftHalf},
                                      Point2f{1.0f - kHeadLength, 0.5f - kShaftHalf},
                                      Point2f{1.0f - kHeadLength, 0.0f},
                                      Point2f{1.0f, 0.5f},
                                      Point2f{1.0f - kHeadLength, 1.0f},
                                      Point2f{1.0f - kHeadLength, 0.5f + kShaftHalf},
                                      Point2f{0.0f, 0.5f + kShaftHalf}};
    std::vector<Point2f> points;
    points.reserve(unit.size());
    for (const Point2f& corner : unit) points.push_back(corner_of(box, kind, corner));
    return points;
}

std::vector<Point2f> regular_polygon(Point2f center, float rx, float ry, int sides,
                                     float start_degrees) {
    std::vector<Point2f> points;
    points.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i)
        points.push_back(point_at(center, rx, ry,
                                  start_degrees + static_cast<float>(i) * 360.0f /
                                                      static_cast<float>(sides)));
    return points;
}

std::vector<Point2f> star_points(Point2f center, float rx, float ry, int spikes,
                                 float inner_ratio) {
    std::vector<Point2f> points;
    points.reserve(static_cast<std::size_t>(spikes) * 2);
    for (int i = 0; i < spikes * 2; ++i) {
        const float radius = i % 2 == 0 ? 1.0f : inner_ratio;
        points.push_back(point_at(center, rx * radius, ry * radius,
                                  -90.0f + static_cast<float>(i) * 180.0f /
                                               static_cast<float>(spikes)));
    }
    return points;
}

std::vector<Point2f> ellipse_points(RectI box) {
    std::vector<Point2f> points;
    append_arc(points, {box.x + box.width / 2.0f, box.y + box.height / 2.0f},
               (box.width - 1) / 2.0f, (box.height - 1) / 2.0f, 0.0f, 360.0f);
    return points;
}

std::vector<Point2f> curve_points(Point2f a, Point2f b) {
    // A single-arc curve: the drag defines the bow, so the curve bulges
    // perpendicular to the drag by a quarter of its length.
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const Point2f control{(a.x + b.x) / 2.0f - dy * 0.25f, (a.y + b.y) / 2.0f + dx * 0.25f};
    constexpr int kSamples = 24;
    std::vector<Point2f> points;
    points.reserve(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const float inverse = 1.0f - t;
        points.push_back({inverse * inverse * a.x + 2.0f * inverse * t * control.x + t * t * b.x,
                          inverse * inverse * a.y + 2.0f * inverse * t * control.y + t * t * b.y});
    }
    return points;
}

std::vector<Point2f> rounded_box_points(RectI box, float radius) {
    const float x0 = static_cast<float>(box.x);
    const float y0 = static_cast<float>(box.y);
    const float x1 = static_cast<float>(box.x + box.width - 1);
    const float y1 = static_cast<float>(box.y + box.height - 1);
    const float r = std::clamp(radius, 0.0f, std::min(x1 - x0, y1 - y0) / 2.0f);
    if (r <= 0.0f) return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::vector<Point2f> points;
    points.push_back({x0 + r, y0});
    points.push_back({x1 - r, y0});
    append_arc(points, {x1 - r, y0 + r}, r, r, 270.0f, 360.0f);
    points.push_back({x1, y1 - r});
    append_arc(points, {x1 - r, y1 - r}, r, r, 0.0f, 90.0f);
    points.push_back({x0 + r, y1});
    append_arc(points, {x0 + r, y1 - r}, r, r, 90.0f, 180.0f);
    points.push_back({x0, y0 + r});
    append_arc(points, {x0 + r, y0 + r}, r, r, 180.0f, 270.0f);
    return points;
}

// Body rectangle of a callout, with the tail below it still inside the drag box.
RectI callout_body(RectI box) {
    const float tail = std::max(4.0f, (box.height - 1) * kTailHeightRatio);
    const int body_height = std::max(1, box.height - static_cast<int>(std::lround(tail)));
    return {box.x, box.y, box.width, body_height};
}

std::vector<Point2f> callout_rounded_points(RectI box) {
    const RectI body = callout_body(box);
    const float x0 = static_cast<float>(body.x);
    const float y0 = static_cast<float>(body.y);
    const float x1 = static_cast<float>(body.x + body.width - 1);
    const float y1 = static_cast<float>(body.y + body.height - 1);
    const float r = std::clamp(kCornerRadius, 0.0f, std::min(x1 - x0, y1 - y0) / 2.0f);
    const float width = static_cast<float>(box.width - 1);
    const float tip_x = x0 + kTailTipRatio * width;
    const float left_x = std::clamp(x0 + kTailLeftRatio * width, x0 + r, x1 - r);
    const float right_x = std::clamp(x0 + kTailRightRatio * width, left_x, x1 - r);
    std::vector<Point2f> points;
    points.push_back({x1 - r, y1});
    points.push_back({right_x, y1});
    points.push_back({tip_x, static_cast<float>(box.y + box.height - 1)});
    points.push_back({left_x, y1});
    append_arc(points, {x0 + r, y1 - r}, r, r, 90.0f, 180.0f);
    points.push_back({x0, y0 + r});
    append_arc(points, {x0 + r, y0 + r}, r, r, 180.0f, 270.0f);
    points.push_back({x1 - r, y0});
    append_arc(points, {x1 - r, y0 + r}, r, r, 270.0f, 360.0f);
    points.push_back({x1, y1 - r});
    append_arc(points, {x1 - r, y1 - r}, r, r, 0.0f, 90.0f);
    return points;
}

std::vector<Point2f> callout_oval_points(RectI box) {
    const RectI body = callout_body(box);
    const Point2f center{body.x + body.width / 2.0f, body.y + body.height / 2.0f};
    std::vector<Point2f> points;
    append_arc(points, center, (body.width - 1) / 2.0f, (body.height - 1) / 2.0f, 160.0f, 470.0f);
    points.push_back({box.x + kTailTipRatio * static_cast<float>(box.width - 1),
                      static_cast<float>(box.y + box.height - 1)});
    return points;
}

std::vector<Point2f> callout_cloud_points(RectI box) {
    // Radius modulated by a fixed angular frequency: the bumps stay in the same
    // place on every call, so a partially swept outline still matches.
    constexpr int kBumps = 9;
    constexpr float kBumpDepth = 0.085f;
    const RectI body = callout_body(box);
    const Point2f center{body.x + body.width / 2.0f, body.y + body.height / 2.0f};
    const float rx = (body.width - 1) / 2.0f;
    const float ry = (body.height - 1) / 2.0f;
    std::vector<Point2f> points;
    for (float angle = 160.0f; angle <= 470.0f; angle += 3.0f) {
        const float bump = 1.0f + kBumpDepth * std::cos(kBumps * angle * kRadians);
        points.push_back(point_at(center, rx * bump, ry * bump, angle));
    }
    points.push_back({box.x + kTailTipRatio * static_cast<float>(box.width - 1),
                      static_cast<float>(box.y + box.height - 1)});
    return points;
}

std::vector<Point2f> heart_points(RectI box) {
    constexpr int kSamples = 64;
    std::vector<Point2f> points;
    points.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        const float t = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSamples);
        const float sine = std::sin(t);
        points.push_back({16.0f * sine * sine * sine,
                          13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) -
                              2.0f * std::cos(3.0f * t) - std::cos(4.0f * t)});
    }
    // The curve is wider than it is tall, so it is fitted into the box with one
    // uniform scale and centred in whichever direction has room to spare.
    float min_x = points.front().x;
    float max_x = min_x;
    float min_y = points.front().y;
    float max_y = min_y;
    for (const Point2f& point : points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    const float width = static_cast<float>(box.width - 1);
    const float height = static_cast<float>(box.height - 1);
    const float scale = std::min(width / (max_x - min_x), height / (max_y - min_y));
    const float offset_x = static_cast<float>(box.x) + (width - (max_x - min_x) * scale) / 2.0f;
    const float offset_y = static_cast<float>(box.y) + (height - (max_y - min_y) * scale) / 2.0f;
    for (Point2f& point : points) {
        const float curve_y = point.y;
        point.x = offset_x + (point.x - min_x) * scale;
        point.y = offset_y + (max_y - curve_y) * scale;
    }
    return points;
}

std::vector<Point2f> lightning_points(RectI box) {
    // A bolt: down the left half to the waist, on to the tip, then back up the
    // right half. The loop never crosses itself, so its interior fills solid.
    const std::array<Point2f, 6> unit{Point2f{0.62f, 0.0f}, Point2f{0.20f, 0.52f},
                                      Point2f{0.42f, 0.52f}, Point2f{0.30f, 1.0f},
                                      Point2f{0.80f, 0.44f}, Point2f{0.56f, 0.44f}};
    std::vector<Point2f> points;
    points.reserve(unit.size());
    const float x0 = static_cast<float>(box.x);
    const float y0 = static_cast<float>(box.y);
    const float width = static_cast<float>(box.width - 1);
    const float height = static_cast<float>(box.height - 1);
    for (const Point2f& corner : unit)
        points.push_back({x0 + corner.x * width, y0 + corner.y * height});
    return points;
}

std::vector<Point2f> polygon_glyph(RectI box) {
    const float x0 = static_cast<float>(box.x);
    const float y0 = static_cast<float>(box.y);
    const float width = static_cast<float>(box.width - 1);
    const float height = static_cast<float>(box.height - 1);
    const std::array<Point2f, 5> unit{Point2f{0.05f, 0.95f}, Point2f{0.05f, 0.35f},
                                      Point2f{0.45f, 0.05f}, Point2f{0.95f, 0.45f},
                                      Point2f{0.85f, 0.95f}};
    std::vector<Point2f> points;
    points.reserve(unit.size());
    for (const Point2f& corner : unit)
        points.push_back({x0 + corner.x * width, y0 + corner.y * height});
    return points;
}

RectI shape_bounds(ShapeId kind, Point2f a, Point2f b, std::span<const Point2f> polygon,
                   int thickness) {
    switch (kind) {
        case ShapeId::Rectangle:
        case ShapeId::Oval:
        case ShapeId::RoundedRect:
            return raster::bounds_rect(shape::normalized_rect(a, b), thickness);
        default: break;
    }
    return raster::bounds_points(shape::outline(kind, a, b, polygon), thickness);
}

void publish(ToolContext& context, const std::vector<Point2f>& points, bool closed,
             PointerButton button) {
    ShapePreview& preview = context.ui.shape;
    const ShapeOptions& options = context.shape_options;
    const bool open = !shape::is_closed(options.kind);
    preview.visible = true;
    preview.draw_fill = !open && options.fill != ShapeTexture::None;
    preview.draw_outline = options.outline != ShapeTexture::None;
    preview.outline_color = context.colors.ink(button);
    preview.fill_color = context.colors.other_ink(button);
    preview.commands = shape::svg_commands(points, closed);
}

// The stroke follows the pixel grid, so along a diagonal edge it can leave a
// one-pixel line of background between itself and the fill. Repaint such a
// pixel with the fill colour: only a pixel that has the fill colour on one side
// of it and the stroke colour on the other is repainted, so the repair cannot
// reach outside the band between the two.
void bridge_fill_to_outline(raster::Target target, RectI area, Rgba8 fill_color,
                            Rgba8 outline_color) {
    area = area.intersected(target.pixels.bounds());
    if (area.empty()) return;
    std::vector<std::pair<int, int>> bridges;
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            const Rgba8 here = target.pixels.at(x, y);
            if (here == fill_color || here == outline_color || !target.allows(x, y)) continue;
            bool fill_left = false, outline_left = false, fill_right = false, outline_right = false;
            bool fill_up = false, outline_up = false, fill_down = false, outline_down = false;
            for (int d = 1; d <= 2; ++d) {
                if (x - d >= area.x) {
                    const Rgba8 pixel = target.pixels.at(x - d, y);
                    fill_left = fill_left || pixel == fill_color;
                    outline_left = outline_left || pixel == outline_color;
                }
                if (x + d < area.right()) {
                    const Rgba8 pixel = target.pixels.at(x + d, y);
                    fill_right = fill_right || pixel == fill_color;
                    outline_right = outline_right || pixel == outline_color;
                }
                if (y - d >= area.y) {
                    const Rgba8 pixel = target.pixels.at(x, y - d);
                    fill_up = fill_up || pixel == fill_color;
                    outline_up = outline_up || pixel == outline_color;
                }
                if (y + d < area.bottom()) {
                    const Rgba8 pixel = target.pixels.at(x, y + d);
                    fill_down = fill_down || pixel == fill_color;
                    outline_down = outline_down || pixel == outline_color;
                }
            }
            const bool sideways = (fill_left && outline_right) || (outline_left && fill_right);
            const bool upright = (fill_up && outline_down) || (outline_up && fill_down);
            if (sideways || upright) bridges.emplace_back(x, y);
        }
    }
    for (const std::pair<int, int>& pixel : bridges) {
        target.pixels.at(pixel.first, pixel.second) = fill_color;
    }
}

// The brush a texture draws with. The profile comes from the brush engine, so a
// shape's crayon outline and a crayon stroke lay down the same grain.
Brush texture_brush(ShapeTexture texture, int thickness) {
    BrushKind kind = BrushKind::Crayon;
    switch (texture) {
        case ShapeTexture::Marker: kind = BrushKind::Marker; break;
        case ShapeTexture::Oil: kind = BrushKind::Oil; break;
        case ShapeTexture::NaturalPencil: kind = BrushKind::NaturalPencil; break;
        case ShapeTexture::Watercolor: kind = BrushKind::Watercolor; break;
        default: break;
    }
    return Brush{thickness, 0.25f, BrushShape::Round, kind};
}

// The clip the raster target applies, in the form a brush stamp understands, so
// a textured part is held back by the same selection the hard edges honour.
SelectionMask target_clip(raster::Target target) {
    const RectI bounds = target.pixels.bounds();
    SelectionMask clip(bounds.width, bounds.height);
    if (target.clip_mask == nullptr) return clip;  // no selection is writable everywhere
    const std::size_t count = static_cast<std::size_t>(bounds.width) * bounds.height;
    clip.replace_region(bounds,
                        std::vector<std::uint8_t>(target.clip_mask, target.clip_mask + count));
    return clip;
}

// Stamps a textured outline along the shape's path.
void stroke_texture(raster::Target target, std::span<const Point2f> points, int thickness,
                    ShapeTexture texture, Rgba8 color, bool closed) {
    if (points.size() < 2) return;
    SelectionMask clip = target_clip(target);
    BrushEngine engine(texture_brush(texture, thickness), color, clip);
    for (const Point2f& point : points) (void)engine.stroke_to(target.pixels, point);
    if (closed) (void)engine.stroke_to(target.pixels, points.front());
}

// The shape's interior, held back by the clip the raster target carries.
SelectionMask interior_clip(raster::Target target, std::span<const Point2f> points) {
    const RectI bounds = target.pixels.bounds();
    SelectionMask interior(bounds.width, bounds.height);
    interior.fill_polygon(std::vector<Point2f>(points.begin(), points.end()));
    if (target.clip_mask == nullptr) return interior;
    std::vector<std::uint8_t> bytes = interior.extract_region(bounds);
    const std::size_t count = bytes.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (target.clip_mask[i] == 0) bytes[i] = 0;
    }
    interior.replace_region(bounds, bytes);
    return interior;
}

// Textured fills cover the interior with parallel strokes. Crayon keeps the gaps
// between its strokes, the other textures let them touch so the interior reads as
// one grainy surface instead of as lines.
// Stamps one stroke per diagonal line, which is the most expensive fill the
// tools have: the work grows with the square of a shape's height. Past this
// budget the hatch is widened, so a very large fill gets a slightly coarser
// texture instead of blocking the UI for seconds.
constexpr float kHatchWorkBudget = 12.0e6f;

void hatch_texture(raster::Target target, std::span<const Point2f> points, int thickness,
                   ShapeTexture texture, Rgba8 color) {
    if (points.size() < 3) return;
    SelectionMask interior = interior_clip(target, points);
    if (interior.empty()) return;
    const RectI box = interior.bounds();
    const Brush brush = texture_brush(texture, thickness);
    // The strokes run down the diagonal, so a perpendicular distance of `gap`
    // needs a step of gap * sqrt(2) along the x axis. A gap just under the brush
    // size leaves no background between the strokes.
    const float gap = texture == ShapeTexture::Crayon
                          ? std::max(3.0f, static_cast<float>(thickness))
                          : std::max(1.0f, static_cast<float>(thickness) * 0.9f);
    float step = gap * 1.41421356f;
    const float top = static_cast<float>(box.y);
    const float bottom = static_cast<float>(box.bottom());
    const float height = bottom - top;
    const float lines = (static_cast<float>(box.width) + height) / step;
    const float work = lines * height * 1.41421356f;
    if (work > kHatchWorkBudget) step *= work / kHatchWorkBudget;
    for (float x = static_cast<float>(box.x) - height; x < static_cast<float>(box.right());
         x += step) {
        // Only the middle of the diagonal lands inside the box, so the strokes
        // at either end are cut to their visible part: a corner line does not
        // stamp its way across the empty region beside the shape.
        const float from = std::max(x, static_cast<float>(box.x));
        const float to = std::min(x + height, static_cast<float>(box.right()));
        if (to - from < 0.5f) continue;
        BrushEngine engine(brush, color, interior);
        (void)engine.stroke_to(target.pixels, Point2f{from, top + (from - x)});
        (void)engine.stroke_to(target.pixels, Point2f{to, top + (to - x)});
    }
}

}  // namespace

RectI shape::normalized_rect(Point2f a, Point2f b) {
    const int left = static_cast<int>(std::lround(std::min(a.x, b.x)));
    const int top = static_cast<int>(std::lround(std::min(a.y, b.y)));
    const int right = static_cast<int>(std::lround(std::max(a.x, b.x)));
    const int bottom = static_cast<int>(std::lround(std::max(a.y, b.y)));
    // The drag corners are the shape's outermost pixels, so a click still has a
    // pixel to draw and the box never collapses to nothing.
    return {left, top, std::max(1, right - left + 1), std::max(1, bottom - top + 1)};
}

bool shape::is_closed(ShapeId kind) {
    return kind != ShapeId::Line && kind != ShapeId::Curve;
}

std::vector<Point2f> shape::outline(ShapeId kind, Point2f a, Point2f b,
                                    std::span<const Point2f> polygon) {
    if (kind == ShapeId::Polygon) return {polygon.begin(), polygon.end()};
    if (kind == ShapeId::Line) return {a, b};
    if (kind == ShapeId::Curve) return curve_points(a, b);

    const RectI box = normalized_rect(a, b);
    const float x0 = static_cast<float>(box.x);
    const float y0 = static_cast<float>(box.y);
    const float x1 = static_cast<float>(box.x + box.width - 1);
    const float y1 = static_cast<float>(box.y + box.height - 1);
    const Point2f center{(x0 + x1) / 2.0f, (y0 + y1) / 2.0f};
    const float rx = (x1 - x0) / 2.0f;
    const float ry = (y1 - y0) / 2.0f;

    switch (kind) {
        case ShapeId::Rectangle: return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        case ShapeId::RoundedRect: return rounded_box_points(box, kCornerRadius);
        case ShapeId::Oval: return ellipse_points(box);
        case ShapeId::Triangle: return {{center.x, y0}, {x1, y1}, {x0, y1}};
        case ShapeId::RightTriangle: return {{x0, y1}, {x0, y0}, {x1, y1}};
        case ShapeId::Diamond:
            return {{center.x, y0}, {x1, center.y}, {center.x, y1}, {x0, center.y}};
        case ShapeId::Pentagon: return regular_polygon(center, rx, ry, 5, -90.0f);
        case ShapeId::Hexagon: return regular_polygon(center, rx, ry, 6, 0.0f);
        case ShapeId::Star4: return star_points(center, rx, ry, 4, 0.38f);
        case ShapeId::Star5: return star_points(center, rx, ry, 5, 0.382f);
        case ShapeId::Star6: return star_points(center, rx, ry, 6, 0.577f);
        case ShapeId::ArrowRight:
        case ShapeId::ArrowLeft:
        case ShapeId::ArrowUp:
        case ShapeId::ArrowDown: return arrow_points(box, kind);
        case ShapeId::CalloutRounded: return callout_rounded_points(box);
        case ShapeId::CalloutOval: return callout_oval_points(box);
        case ShapeId::CalloutCloud: return callout_cloud_points(box);
        case ShapeId::Heart: return heart_points(box);
        case ShapeId::Lightning: return lightning_points(box);
        case ShapeId::Line:
        case ShapeId::Curve:
        case ShapeId::Polygon: break;
    }
    return {};
}

std::string shape::svg_commands(std::span<const Point2f> points, bool closed) {
    std::string commands;
    char buffer[64];
    for (std::size_t i = 0; i < points.size(); ++i) {
        std::snprintf(buffer, sizeof(buffer), "%s%.2f %.2f", i == 0 ? "M" : "L", points[i].x,
                      points[i].y);
        if (!commands.empty()) commands.push_back(' ');
        commands += buffer;
    }
    if (closed && !points.empty()) commands += " Z";
    return commands;
}

std::string shape::gallery_commands(ShapeId kind, RectI box) {
    if (kind == ShapeId::Polygon) return svg_commands(polygon_glyph(box), true);
    const Point2f top_left{static_cast<float>(box.x), static_cast<float>(box.y)};
    const Point2f bottom_right{static_cast<float>(box.x + box.width - 1),
                               static_cast<float>(box.y + box.height - 1)};
    return svg_commands(outline(kind, top_left, bottom_right, {}), is_closed(kind));
}

void rasterize_shape(raster::Target target, ShapeId kind, Point2f a, Point2f b,
                     std::span<const Point2f> polygon, const ShapeOptions& options,
                     Rgba8 outline_color, Rgba8 fill_color) {
    // An open shape has no interior, so a fill texture cannot be laid down.
    const bool open = !shape::is_closed(kind);
    const bool wants_outline = options.outline != ShapeTexture::None;
    const bool wants_fill = !open && options.fill != ShapeTexture::None;
    const bool solid_outline = options.outline == ShapeTexture::Solid;
    const bool solid_fill = options.fill == ShapeTexture::Solid;
    const int thickness = std::clamp(options.thickness, 1, 32);
    const RectI box = shape::normalized_rect(a, b);
    const bool plain = (solid_outline || !wants_outline) && (solid_fill || !wants_fill);

    // Rectangle, ellipse and rounded rectangle keep their own primitives: they
    // are exact at every size, where a sampled outline would cut thin corners.
    // A texture is stamped along the sampled path instead.
    if (plain) {
        switch (kind) {
        case ShapeId::Rectangle:
            if (wants_fill) raster::draw_rect_fill(target, box, fill_color);
            if (wants_outline) raster::draw_rect_outline(target, box, thickness, outline_color);
            return;
        case ShapeId::Oval:
            if (wants_fill) raster::draw_ellipse_fill(target, box, fill_color);
            if (wants_outline)
                raster::draw_ellipse_outline(target, box, thickness, outline_color);
            return;
        case ShapeId::RoundedRect:
            if (wants_fill)
                raster::draw_rounded_rect_fill(target, box, options.corner_radius, fill_color);
            if (wants_outline)
                raster::draw_rounded_rect_outline(target, box, options.corner_radius, thickness,
                                                  outline_color);
            return;
        default: break;
        }
    }

    const std::vector<Point2f> points = shape::outline(kind, a, b, polygon);
    if (points.size() < 2) return;
    if (wants_fill) {
        if (solid_fill) {
            raster::draw_polygon_fill(target, points, fill_color);
        } else {
            hatch_texture(target, points, thickness, options.fill, fill_color);
        }
    }
    if (wants_outline) {
        if (solid_outline) {
            raster::draw_polyline(target, points, thickness, outline_color, !open);
        } else {
            stroke_texture(target, points, thickness, options.outline, outline_color, !open);
        }
    }
    // The repair only has the two flat colors to go by, so it runs for the solid
    // pair; a texture covers its own boundary with its own grain.
    if (wants_fill && wants_outline && solid_fill && solid_outline) {
        RectI area{};
        for (const Point2f& point : points) {
            const int x = static_cast<int>(std::floor(point.x));
            const int y = static_cast<int>(std::floor(point.y));
            area = area.united(RectI{x, y, 1, 1});
        }
        const int pad = thickness + 1;
        bridge_fill_to_outline(target, RectI{area.x - pad, area.y - pad, area.width + 2 * pad,
                                             area.height + 2 * pad},
                               fill_color, outline_color);
    }
}

void ShapeTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    if (context.shape_options.kind != ShapeId::Polygon) {
        // The tool was switched back to a drag shape while a polygon was open.
        if (m_polygon_open) cancel_polygon(context);
        start_drag(context, position, button);
        return;
    }
    if (!m_polygon_open) {
        m_polygon_open = true;
        m_polygon = {position};
        m_button = button;
        update_preview(context, position);
        return;
    }
    if (m_polygon.size() >= 3 && distance(position, m_polygon.front()) <= 8.0f) {
        close_polygon(context);
        return;
    }
    // Two clicks in the same spot are the double-click that closes the polygon.
    if (distance(position, m_polygon.back()) <= 3.0f) return;
    m_polygon.push_back(position);
    update_preview(context, position);
}

void ShapeTool::on_pointer_move(ToolContext& context, Point2f position) {
    if (!m_dragging && !m_polygon_open) return;
    update_preview(context, position);
}

void ShapeTool::on_pointer_up(ToolContext& context, Point2f position) {
    if (m_polygon_open) {
        // The polygon only closes on an explicit click, Enter or double-click.
        update_preview(context, position);
        return;
    }
    if (!m_dragging) return;
    m_dragging = false;
    commit_drag(context, position);
    context.ui.shape.visible = false;
}

void ShapeTool::on_key_pressed(ToolContext& context, std::string_view text) {
    if (!m_polygon_open) return;
    if (text == "\n") {
        close_polygon(context);
        return;
    }
    if (text == "\u001b") cancel_polygon(context);
}

void ShapeTool::on_double_click(ToolContext& context) {
    if (!m_polygon_open) return;
    // Slint reports the double click after the second press already placed a
    // vertex, so drop that near-duplicate before closing.
    if (m_polygon.size() >= 2 && distance(m_polygon.back(), m_polygon[m_polygon.size() - 2]) <= 12.0f)
        m_polygon.pop_back();
    close_polygon(context);
}

void ShapeTool::on_deactivate(ToolContext& context) {
    cancel_polygon(context);
    m_dragging = false;
    context.ui.shape.visible = false;
}

void ShapeTool::start_drag(ToolContext& context, Point2f position, PointerButton button) {
    m_dragging = true;
    m_button = button;
    m_origin = position;
    update_preview(context, position);
}

void ShapeTool::update_preview(ToolContext& context, Point2f cursor) {
    m_cursor = cursor;
    if (m_polygon_open) {
        std::vector<Point2f> points = m_polygon;
        points.push_back(cursor);
        publish(context, points, false, m_button);
        return;
    }
    publish(context, shape::outline(context.shape_options.kind, m_origin, cursor, {}), 
            shape::is_closed(context.shape_options.kind), m_button);
}

void ShapeTool::commit_drag(ToolContext& context, Point2f end) {
    m_cursor = end;
    const ShapeId kind = context.shape_options.kind;
    const ShapeOptions& options = context.shape_options;
    const RectI region =
        shape_bounds(kind, m_origin, end, {}, std::clamp(options.thickness, 1, 32))
            .intersected(context.document.bounds());
    if (region.empty()) return;

    const Rgba8 outline = context.colors.ink(m_button);
    const Rgba8 fill = context.colors.other_ink(m_button);
    const std::vector<std::uint8_t> clip = context.selection_clip();
    raster::Target target{context.document.active_layer().pixels,
                          clip.empty() ? nullptr : clip.data()};
    StrokeRecorder recorder(context.document);
    rasterize_shape(target, kind, m_origin, end, {}, options, outline, fill);
    recorder.touch(region);
    recorder.commit(context.history);
}

void ShapeTool::close_polygon(ToolContext& context) {
    if (!m_polygon_open) return;
    const std::vector<Point2f> vertices = m_polygon;
    m_polygon_open = false;
    m_polygon.clear();
    context.ui.shape.visible = false;
    if (vertices.size() < 3) return;

    const ShapeOptions& options = context.shape_options;
    const RectI region = shape_bounds(ShapeId::Polygon, m_origin, m_cursor, vertices,
                                      std::clamp(options.thickness, 1, 32))
                             .intersected(context.document.bounds());
    if (region.empty()) return;

    const Rgba8 outline = context.colors.ink(m_button);
    const Rgba8 fill = context.colors.other_ink(m_button);
    const std::vector<std::uint8_t> clip = context.selection_clip();
    raster::Target target{context.document.active_layer().pixels,
                          clip.empty() ? nullptr : clip.data()};
    StrokeRecorder recorder(context.document);
    rasterize_shape(target, ShapeId::Polygon, m_origin, m_cursor, vertices, options, outline, fill);
    recorder.touch(region);
    recorder.commit(context.history);
}

void ShapeTool::cancel_polygon(ToolContext& context) {
    if (!m_polygon_open) return;
    m_polygon_open = false;
    m_polygon.clear();
    context.ui.shape.visible = false;
}

bool ShapeTool::on_cancel(ToolContext& context) {
    // A drag paints nothing until the release - it only publishes a preview -
    // so abandoning it is dropping that preview.
    if (!m_dragging && !m_polygon_open) return false;
    cancel_polygon(context);
    m_dragging = false;
    context.ui.shape.visible = false;
    return true;
}

}  // namespace lsp
