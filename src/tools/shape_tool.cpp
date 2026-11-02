#include "tools/shape_tool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

#include "brushes/brush_engine.hpp"
#include "selection/selection_mask.hpp"
#include "selection/selection_ops.hpp"
#include "tools/stroke_recorder.hpp"
#include "tools/tool_context.hpp"
#include "utils/key_text.hpp"
#include "utils/math.hpp"

namespace lsp {

namespace {

// The corner of the rounded rectangle and callout kinds.
constexpr float kCornerRadius = static_cast<float>(kDefaultCornerRadius);
// The callout tail hangs below the body inside the same drag rectangle.
constexpr float kTailHeightRatio = 0.25f;
constexpr float kTailTipRatio = 0.12f;
constexpr float kTailLeftRatio = 0.20f;
constexpr float kTailRightRatio = 0.44f;

// How close a click has to be to the first vertex to close a polygon, to the
// last vertex to count as a repeat of it, and how close the last two vertices
// have to be for the second to be the stray one a double click leaves behind.
constexpr float kClosePolygonDistance = 8.0f;
constexpr float kRepeatedVertexDistance = 3.0f;
constexpr float kDoubleClickVertexDistance = 12.0f;

// Textured fills: the brush strokes of a hatch are at least this far apart for
// the crayon, whose gaps show, and this share of the brush size apart for the
// textures that are meant to touch. A stroke shorter than the minimum length
// is not worth stamping.
constexpr float kCrayonMinGap = 3.0f;
constexpr float kTouchingGapRatio = 0.9f;
constexpr float kMinHatchStrokeLength = 0.5f;
// How far either side of a pixel the repair of a solid fill looks for the
// other colour.
constexpr int kBridgeReach = 2;

int clamped_thickness(const ShapeOptions& options) {
    return std::clamp(options.thickness, kMinShapeThickness, kMaxShapeThickness);
}

// The drag box as the positions of its first and last pixels.
struct BoxEdges {
    float left;
    float top;
    float right;
    float bottom;

    [[nodiscard]] float width() const { return right - left; }
    [[nodiscard]] float height() const { return bottom - top; }
};

BoxEdges edges_of(RectI box) {
    return {static_cast<float>(box.x), static_cast<float>(box.y), static_cast<float>(box.x + box.width - 1),
            static_cast<float>(box.y + box.height - 1)};
}

Point2f point_at(Point2f center, float rx, float ry, float degrees) {
    const float radians = radians_from_degrees(degrees);
    return {center.x + rx * std::cos(radians), center.y + ry * std::sin(radians)};
}

// The unit-square points laid over a drag box, whose far edges are its last
// pixels.
template <std::size_t Count>
std::vector<Point2f> fit_to_box(RectI box, const std::array<Point2f, Count>& unit) {
    const BoxEdges edges = edges_of(box);
    std::vector<Point2f> points;
    points.reserve(unit.size());
    for (const Point2f& corner : unit)
        points.push_back({edges.left + corner.x * edges.width(), edges.top + corner.y * edges.height()});
    return points;
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
    const BoxEdges e = edges_of(box);
    const float x0 = e.left;
    const float y0 = e.top;
    const float width = e.width();
    const float height = e.height();
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
                                  start_degrees + static_cast<float>(i) * kFullTurnDegrees /
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
                                  -kQuarterTurnDegrees + static_cast<float>(i) * kHalfTurnDegrees /
                                               static_cast<float>(spikes)));
    }
    return points;
}

std::vector<Point2f> ellipse_points(RectI box) {
    std::vector<Point2f> points;
    append_arc(points, {box.x + box.width / 2.0f, box.y + box.height / 2.0f},
               (box.width - 1) / 2.0f, (box.height - 1) / 2.0f, 0.0f, kFullTurnDegrees);
    return points;
}

// The quadratic Bezier from `a` to `b` bent towards `control`.
std::vector<Point2f> curve_points(Point2f a, Point2f control, Point2f b) {
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

// Where a curve between `a` and `b` bends when nothing says otherwise: out to
// the side of the drag by a quarter of its length.
Point2f default_bend(Point2f a, Point2f b) {
    constexpr float kBow = 0.25f;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const Point2f middle = midpoint(a, b);
    return {middle.x - dy * kBow, middle.y + dx * kBow};
}

std::vector<Point2f> rounded_box_points(RectI box, float radius) {
    const auto [x0, y0, x1, y1] = edges_of(box);
    const float r = std::clamp(radius, 0.0f, std::min(x1 - x0, y1 - y0) / 2.0f);
    if (r <= 0.0f) return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::vector<Point2f> points;
    points.push_back({x0 + r, y0});
    points.push_back({x1 - r, y0});
    append_arc(points, {x1 - r, y0 + r}, r, r, kThreeQuarterTurnDegrees, kFullTurnDegrees);
    points.push_back({x1, y1 - r});
    append_arc(points, {x1 - r, y1 - r}, r, r, 0.0f, kQuarterTurnDegrees);
    points.push_back({x0 + r, y1});
    append_arc(points, {x0 + r, y1 - r}, r, r, kQuarterTurnDegrees, kHalfTurnDegrees);
    points.push_back({x0, y0 + r});
    append_arc(points, {x0 + r, y0 + r}, r, r, kHalfTurnDegrees, kThreeQuarterTurnDegrees);
    return points;
}

// Body rectangle of a callout, with the tail below it still inside the drag box.
RectI callout_body(RectI box) {
    const float tail = std::max(4.0f, (box.height - 1) * kTailHeightRatio);
    const int body_height = std::max(1, box.height - round_to_int(tail));
    return {box.x, box.y, box.width, body_height};
}

std::vector<Point2f> callout_rounded_points(RectI box) {
    const RectI body = callout_body(box);
    const auto [x0, y0, x1, y1] = edges_of(body);
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
    append_arc(points, {x0 + r, y1 - r}, r, r, kQuarterTurnDegrees, kHalfTurnDegrees);
    points.push_back({x0, y0 + r});
    append_arc(points, {x0 + r, y0 + r}, r, r, kHalfTurnDegrees, kThreeQuarterTurnDegrees);
    points.push_back({x1 - r, y0});
    append_arc(points, {x1 - r, y0 + r}, r, r, kThreeQuarterTurnDegrees, kFullTurnDegrees);
    points.push_back({x1, y1 - r});
    append_arc(points, {x1 - r, y1 - r}, r, r, 0.0f, kQuarterTurnDegrees);
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
        const float bump = 1.0f + kBumpDepth * std::cos(kBumps * radians_from_degrees(angle));
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
        const float t = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
                        static_cast<float>(kSamples);
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
    const BoxEdges edges = edges_of(box);
    const float width = edges.width();
    const float height = edges.height();
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
    return fit_to_box(box, unit);
}

std::vector<Point2f> polygon_glyph(RectI box) {
    const std::array<Point2f, 5> unit{Point2f{0.05f, 0.95f}, Point2f{0.05f, 0.35f},
                                      Point2f{0.45f, 0.05f}, Point2f{0.95f, 0.45f},
                                      Point2f{0.85f, 0.95f}};
    return fit_to_box(box, unit);
}

// The middle of a drag box's pixels, which the outline of every closed kind is
// laid out around.
Point2f middle_of(RectI box) {
    return {static_cast<float>(box.x) + static_cast<float>(box.width - 1) / 2.0f,
            static_cast<float>(box.y) + static_cast<float>(box.height - 1) / 2.0f};
}

// The outline turned about the middle of the drag box.
std::vector<Point2f> turned_outline(ShapeId kind, Point2f a, Point2f b,
                                    std::span<const Point2f> polygon, float rotation) {
    std::vector<Point2f> points = shape::outline(kind, a, b, polygon);
    if (rotation == 0.0f || !shape::is_closed(kind)) return points;
    const RectI box = shape::normalized_rect(a, b);
    for (Point2f& point : points) point = shape::turned_about_middle(box, point, rotation);
    return points;
}

RectI shape_bounds(ShapeId kind, Point2f a, Point2f b, std::span<const Point2f> polygon,
                   int thickness, float rotation = 0.0f) {
    if (rotation == 0.0f) {
        switch (kind) {
            case ShapeId::Rectangle:
            case ShapeId::Oval:
            case ShapeId::RoundedRect:
                return raster::bounds_rect(shape::normalized_rect(a, b), thickness);
            default: break;
        }
    }
    return raster::bounds_points(turned_outline(kind, a, b, polygon, rotation), thickness);
}

// Paints the finished shape into the active layer as one history entry.
void commit_shape(ToolContext& context, ShapeId kind, Point2f a, Point2f b,
                  std::span<const Point2f> polygon, const ShapePaint& paint, float rotation = 0.0f) {
    const RectI region = shape_bounds(kind, a, b, polygon, clamped_thickness(paint.options), rotation)
                             .intersected(context.document.bounds());
    if (region.empty()) return;

    const LayerTarget layer = context.layer_target();
    StrokeRecorder recorder(context.document);
    rasterize_shape(layer.target(), kind, a, b, polygon, paint.options, paint.outline, paint.fill,
                    rotation);
    recorder.touch(region);
    recorder.commit(context.history);
}

// Whether pixels of the two colours lie within kBridgeReach of a pixel in one
// direction.
struct Neighbours {
    bool fill = false;
    bool outline = false;
};

Neighbours look(const PixelBuffer& pixels, RectI area, int x, int y, int step_x, int step_y,
                Rgba8 fill_color, Rgba8 outline_color) {
    Neighbours found;
    for (int distance = 1; distance <= kBridgeReach; ++distance) {
        const int px = x + step_x * distance;
        const int py = y + step_y * distance;
        if (!area.contains(px, py)) continue;
        const Rgba8 pixel = pixels.at(px, py);
        found.fill = found.fill || pixel == fill_color;
        found.outline = found.outline || pixel == outline_color;
    }
    return found;
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
            const auto probe = [&](int step_x, int step_y) {
                return look(target.pixels, area, x, y, step_x, step_y, fill_color, outline_color);
            };
            const Neighbours left = probe(-1, 0);
            const Neighbours right = probe(1, 0);
            const Neighbours up = probe(0, -1);
            const Neighbours down = probe(0, 1);
            const bool sideways = (left.fill && right.outline) || (left.outline && right.fill);
            const bool upright = (up.fill && down.outline) || (up.outline && down.fill);
            if (sideways || upright) bridges.emplace_back(x, y);
        }
    }
    for (const auto& [x, y] : bridges) target.pixels.at(x, y) = fill_color;
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
    return Brush{thickness, kDefaultBrushSpacing, BrushShape::Round, kind};
}

// The clip the raster target applies, in the form a brush stamp understands, so
// a textured part is held back by the same selection the hard edges honour.
SelectionMask target_clip(raster::Target target) {
    if (target.clip != nullptr) return *target.clip;
    return SelectionMask(target.pixels.width(), target.pixels.height());
}

// Stamps a textured outline along the shape's path.
void stroke_texture(raster::Target target, std::span<const Point2f> points, int thickness,
                    ShapeTexture texture, Rgba8 color, bool closed) {
    if (points.size() < 2) return;
    SelectionMask clip = target_clip(target);
    BrushEngine engine(texture_brush(texture, thickness), color, clip);
    for (const Point2f& point : points) (void)engine.stroke_to(target.pixels, point);
    if (closed) (void)engine.stroke_to(target.pixels, points.front());
    (void)engine.finish(target.pixels);
}

// The shape's interior, held back by the clip the raster target carries.
SelectionMask interior_clip(raster::Target target, std::span<const Point2f> points) {
    const RectI bounds = target.pixels.bounds();
    SelectionMask interior(bounds.width, bounds.height);
    interior.fill_polygon(points);
    if (target.clip != nullptr) interior.intersect(*target.clip);
    return interior;
}

// Stamps one stroke per diagonal line, which is the most expensive fill the
// tools have: the work grows with the square of a shape's height. Past this
// budget the hatch is widened, so a very large fill gets a slightly coarser
// texture instead of blocking the UI for seconds.
constexpr float kHatchWorkBudget = 12.0e6f;

// Covers the interior with parallel strokes. Crayon keeps the gaps between its
// strokes, the other textures let them touch so the interior reads as one
// grainy surface instead of as lines.
void hatch_texture(raster::Target target, std::span<const Point2f> points, int thickness,
                   ShapeTexture texture, Rgba8 color) {
    if (points.size() < kMinPolygonPoints) return;
    SelectionMask interior = interior_clip(target, points);
    if (interior.empty()) return;
    const RectI box = interior.bounds();
    const Brush brush = texture_brush(texture, thickness);
    // The strokes run down the diagonal, so a perpendicular distance of `gap`
    // needs a step of gap * sqrt(2) along the x axis. A gap just under the brush
    // size leaves no background between the strokes.
    const float gap = texture == ShapeTexture::Crayon
                          ? std::max(kCrayonMinGap, static_cast<float>(thickness))
                          : std::max(1.0f, static_cast<float>(thickness) * kTouchingGapRatio);
    float step = gap * std::numbers::sqrt2_v<float>;
    const float top = static_cast<float>(box.y);
    const float bottom = static_cast<float>(box.bottom());
    const float height = bottom - top;
    const float lines = (static_cast<float>(box.width) + height) / step;
    const float work = lines * height * std::numbers::sqrt2_v<float>;
    if (work > kHatchWorkBudget) step *= work / kHatchWorkBudget;
    for (float x = static_cast<float>(box.x) - height; x < static_cast<float>(box.right());
         x += step) {
        // Only the middle of the diagonal lands inside the box, so the strokes
        // at either end are cut to their visible part: a corner line does not
        // stamp its way across the empty region beside the shape.
        const float from = std::max(x, static_cast<float>(box.x));
        const float to = std::min(x + height, static_cast<float>(box.right()));
        if (to - from < kMinHatchStrokeLength) continue;
        BrushEngine engine(brush, color, interior);
        (void)engine.stroke_to(target.pixels, Point2f{from, top + (from - x)});
        (void)engine.stroke_to(target.pixels, Point2f{to, top + (to - x)});
        (void)engine.finish(target.pixels);
    }
}

}  // namespace

RectI shape::normalized_rect(Point2f a, Point2f b) {
    const int left = round_to_int(std::min(a.x, b.x));
    const int top = round_to_int(std::min(a.y, b.y));
    const int right = round_to_int(std::max(a.x, b.x));
    const int bottom = round_to_int(std::max(a.y, b.y));
    // The drag corners are the shape's outermost pixels, so a click still has a
    // pixel to draw and the box never collapses to nothing.
    return {left, top, std::max(1, right - left + 1), std::max(1, bottom - top + 1)};
}

bool shape::is_closed(ShapeId kind) {
    return kind != ShapeId::Line && kind != ShapeId::Curve;
}

Point2f shape::turned_about_middle(RectI box, Point2f point, float degrees) {
    if (degrees == 0.0f) return point;
    const Point2f middle = middle_of(box);
    const float radians = radians_from_degrees(degrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float dx = point.x - middle.x;
    const float dy = point.y - middle.y;
    return {middle.x + dx * cosine - dy * sine, middle.y + dx * sine + dy * cosine};
}

std::vector<Point2f> shape::outline(ShapeId kind, Point2f a, Point2f b,
                                    std::span<const Point2f> polygon) {
    if (kind == ShapeId::Polygon) return {polygon.begin(), polygon.end()};
    if (kind == ShapeId::Line) return {a, b};
    if (kind == ShapeId::Curve) {
        return curve_points(a, polygon.size() == 1 ? polygon.front() : default_bend(a, b), b);
    }

    const RectI box = normalized_rect(a, b);
    const auto [x0, y0, x1, y1] = edges_of(box);
    const Point2f center = midpoint({x0, y0}, {x1, y1});
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
        case ShapeId::Pentagon: return regular_polygon(center, rx, ry, 5, -kQuarterTurnDegrees);
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
    const BoxEdges edges = edges_of(box);
    return svg_commands(outline(kind, top_left, {edges.right, edges.bottom}, {}), is_closed(kind));
}

void rasterize_shape(raster::Target target, ShapeId kind, Point2f a, Point2f b,
                     std::span<const Point2f> polygon, const ShapeOptions& options,
                     Rgba8 outline_color, Rgba8 fill_color, float rotation_degrees) {
    // An open shape has no interior, so a fill texture cannot be laid down.
    const bool open = !shape::is_closed(kind);
    const bool wants_outline = options.outline != ShapeTexture::None;
    const bool wants_fill = !open && options.fill != ShapeTexture::None;
    const bool solid_outline = options.outline == ShapeTexture::Solid;
    const bool solid_fill = options.fill == ShapeTexture::Solid;
    const int thickness = clamped_thickness(options);
    const RectI box = shape::normalized_rect(a, b);
    const bool plain = rotation_degrees == 0.0f && (solid_outline || !wants_outline) &&
                       (solid_fill || !wants_fill);

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

    const std::vector<Point2f> points = turned_outline(kind, a, b, polygon, rotation_degrees);
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
            area = area.united(RectI{floor_to_int(point.x), floor_to_int(point.y), 1, 1});
        }
        const int pad = thickness + 1;
        bridge_fill_to_outline(target, RectI{area.x - pad, area.y - pad, area.width + 2 * pad,
                                             area.height + 2 * pad},
                               fill_color, outline_color);
    }
}

namespace {

// The look a shape drawn by `button` gets from the options and colors as they
// stand now.
ShapePaint paint_for(const ToolContext& context, PointerButton button) {
    return {context.shape_options, context.colors.ink(button), context.colors.other_ink(button)};
}

// The pixel a point lands on.
Point2f pixel_of(Point2f point) {
    return {static_cast<float>(round_to_int(point.x)), static_cast<float>(round_to_int(point.y))};
}

// Paints the preview with the same code the commit runs: on a transparent sheet
// laid over the document, held back by the same selection. The sheet only
// costs memory where it is painted.
template <typename Paint>
void publish_preview(ToolContext& context, RectI area, int thickness, Paint paint) {
    static std::uint64_t revision = 0;
    ShapePreview& preview = context.ui.shape;
    preview.visible = true;
    preview.pixels = PixelBuffer();
    preview.region = {};
    preview.frame.reset();
    preview.points.clear();
    preview.rotation = 0.0f;
    preview.revision = ++revision;
    if (area.empty()) return;

    // Textured edges reach a little past the geometry.
    const int pad = 2 * thickness + 2;
    preview.region = RectI{area.x - pad, area.y - pad, area.width + 2 * pad, area.height + 2 * pad}
                         .intersected(context.document.bounds());
    if (preview.region.empty()) return;

    PixelBuffer sheet(context.document.width(), context.document.height(), kTransparent);
    const LayerTarget layer = context.layer_target();
    paint(raster::Target{sheet, &layer.clip});
    preview.pixels = sheet.copy_region(preview.region);
}

void publish_shape(ToolContext& context, ShapeId kind, Point2f a, Point2f b,
                   std::span<const Point2f> polygon, const ShapePaint& paint,
                   float rotation = 0.0f) {
    const int thickness = clamped_thickness(paint.options);
    publish_preview(context, shape_bounds(kind, a, b, polygon, thickness, rotation), thickness,
                    [&](raster::Target target) {
                        rasterize_shape(target, kind, a, b, polygon, paint.options, paint.outline,
                                        paint.fill, rotation);
                    });
}

// A polygon still being laid out is only its edges so far.
void publish_open_polygon(ToolContext& context, std::span<const Point2f> points,
                          const ShapePaint& paint) {
    const ShapeOptions& options = paint.options;
    const int thickness = clamped_thickness(options);
    publish_preview(context, raster::bounds_points(points, thickness), thickness,
                    [&](raster::Target target) {
        if (options.outline == ShapeTexture::None || points.size() < 2) return;
        if (options.outline == ShapeTexture::Solid) {
            raster::draw_polyline(target, points, thickness, paint.outline, false);
        } else {
            stroke_texture(target, points, thickness, options.outline, paint.outline, false);
        }
    });
}

// A pointer shape along the line, for the grip at one of its ends.
ResizeCursor line_cursor(Point2f a, Point2f b) {
    const float dx = std::abs(b.x - a.x);
    const float dy = std::abs(b.y - a.y);
    if (dx >= 2.0f * dy) return ResizeCursor::Horizontal;
    if (dy >= 2.0f * dx) return ResizeCursor::Vertical;
    return (b.x - a.x) * (b.y - a.y) >= 0.0f ? ResizeCursor::DiagonalNwSe : ResizeCursor::DiagonalNeSw;
}

// How far a point is from the nearest point of the segment.
float distance_to_segment(Point2f point, Point2f a, Point2f b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length_squared = dx * dx + dy * dy;
    float t = 0.0f;
    if (length_squared > 0.0f) t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / length_squared;
    t = std::clamp(t, 0.0f, 1.0f);
    return distance(point, {a.x + t * dx, a.y + t * dy});
}

// Where a resized drag box may reach: the pointer is not held to the canvas, as
// it is not while a shape is first dragged out.
constexpr RectI kUnboundedBox{-(1 << 20), -(1 << 20), 1 << 21, 1 << 21};

}  // namespace

void ShapeTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    if (m_pending) {
        if (grab_pending(context, position)) return;
        commit_pending(context);
    }
    if (m_curve_pending) {
        if (context.shape_options.kind == ShapeId::Curve) {
            m_bending = true;
            m_curve_control = position;
            update_curve_preview(context);
            return;
        }
        // The tool was switched to another shape while a curve was waiting.
        cancel_curve(context);
    }
    if (context.shape_options.kind != ShapeId::Polygon) {
        // The tool was switched back to a drag shape while a polygon was open.
        if (m_polygon_open) cancel_polygon(context);
        start_drag(context, position, button);
        return;
    }
    if (!m_polygon_open) {
        m_polygon_open = true;
        m_polygon = {position};
        m_paint = paint_for(context, button);
        update_preview(context, position);
        return;
    }
    if (m_polygon.size() >= kMinPolygonPoints && distance(position, m_polygon.front()) <= kClosePolygonDistance) {
        close_polygon(context);
        return;
    }
    // Two clicks in the same spot are the double-click that closes the polygon.
    if (distance(position, m_polygon.back()) <= kRepeatedVertexDistance) return;
    m_polygon.push_back(position);
    update_preview(context, position);
}

void ShapeTool::on_pointer_move(ToolContext& context, Point2f position) {
    if (m_edit != Edit::None) {
        edit_pending(context, position);
        return;
    }
    if (m_bending) {
        m_curve_control = position;
        update_curve_preview(context);
        return;
    }
    if (m_pending) {
        hover_pending(context, position);
        return;
    }
    if (!m_dragging && !m_polygon_open) return;
    update_preview(context, position);
}

void ShapeTool::on_pointer_up(ToolContext& context, Point2f position) {
    if (m_edit != Edit::None) {
        edit_pending(context, position);
        m_edit = Edit::None;
        context.ui.move.dragging = false;
        context.ui.rotate.rotating = false;
        hover_pending(context, position);
        return;
    }
    if (m_bending) {
        m_curve_control = position;
        commit_curve(context);
        return;
    }
    if (m_polygon_open) {
        // The polygon only closes on an explicit click, Enter or double-click.
        update_preview(context, position);
        return;
    }
    if (!m_dragging) return;
    m_dragging = false;
    if (m_kind == ShapeId::Curve && distance(m_origin, position) >= 1.0f) {
        // The first drag only lays out the baseline; the next one bends it.
        m_curve_pending = true;
        m_curve_end = position;
        m_curve_control = midpoint(m_origin, position);
        update_curve_preview(context);
        return;
    }
    finish_drag(context, position);
}

void ShapeTool::on_key_pressed(ToolContext& context, std::string_view text) {
    if (m_pending) {
        if (text == kKeyEnter) commit_pending(context);
        if (text == kKeyEscape) cancel_pending(context);
        return;
    }
    if (m_curve_pending) {
        if (text == kKeyEnter) commit_curve(context);
        if (text == kKeyEscape) cancel_curve(context);
        return;
    }
    if (!m_polygon_open) return;
    if (text == kKeyEnter) {
        close_polygon(context);
        return;
    }
    if (text == kKeyEscape) cancel_polygon(context);
}

void ShapeTool::on_double_click(ToolContext& context) {
    if (m_curve_pending) {
        commit_curve(context);
        return;
    }
    if (!m_polygon_open) return;
    // Slint reports the double click after the second press already placed a
    // vertex, so drop that near-duplicate before closing.
    if (m_polygon.size() >= 2 &&
        distance(m_polygon.back(), m_polygon[m_polygon.size() - 2]) <= kDoubleClickVertexDistance)
        m_polygon.pop_back();
    close_polygon(context);
}

void ShapeTool::on_deactivate(ToolContext& context) {
    commit_pending(context);
    cancel_polygon(context);
    cancel_curve(context);
    m_bending = false;
    m_dragging = false;
    hide_preview(context);
}

Point2f ShapeTool::constrained_end(const ToolContext& context, Point2f end) const {
    if (!context.shift || !shape::is_closed(m_kind)) return end;
    const int x = round_to_int(m_origin.x);
    const int y = round_to_int(m_origin.y);
    const int dx = round_to_int(end.x) - x;
    const int dy = round_to_int(end.y) - y;
    const float side = static_cast<float>(std::max(std::abs(dx), std::abs(dy)));
    return {x + std::copysign(side, end.x - m_origin.x),
            y + std::copysign(side, end.y - m_origin.y)};
}

void ShapeTool::start_drag(ToolContext& context, Point2f position, PointerButton button) {
    m_dragging = true;
    m_kind = context.shape_options.kind;
    m_paint = paint_for(context, button);
    m_rotation = 0.0f;
    m_origin = position;
    update_preview(context, position);
}

void ShapeTool::update_preview(ToolContext& context, Point2f cursor) {
    m_cursor = cursor;
    if (m_polygon_open) {
        std::vector<Point2f> points = m_polygon;
        points.push_back(cursor);
        publish_open_polygon(context, points, m_paint);
        return;
    }
    cursor = constrained_end(context, cursor);
    // A curve is a straight baseline until it is bent.
    const Point2f middle = midpoint(m_origin, cursor);
    const std::span<const Point2f> bend =
        m_kind == ShapeId::Curve ? std::span<const Point2f>(&middle, 1) : std::span<const Point2f>();
    publish_shape(context, m_kind, m_origin, cursor, bend, m_paint);
    if (!shape::is_closed(m_kind)) context.ui.shape.points = {pixel_of(m_origin), pixel_of(cursor)};
}

void ShapeTool::update_curve_preview(ToolContext& context) {
    publish_shape(context, ShapeId::Curve, m_origin, m_curve_end, {&m_curve_control, 1}, m_paint);
    context.ui.shape.points = {pixel_of(m_origin), pixel_of(m_curve_end), pixel_of(m_curve_control)};
}

void ShapeTool::finish_drag(ToolContext& context, Point2f end) {
    m_cursor = end;
    m_end = constrained_end(context, end);
    m_pending = true;
    publish_pending(context);
}

void ShapeTool::commit_curve(ToolContext& context) {
    const Point2f control = m_curve_control;
    m_curve_pending = false;
    m_bending = false;
    hide_preview(context);
    commit_shape(context, ShapeId::Curve, m_origin, m_curve_end, {&control, 1}, m_paint);
}

void ShapeTool::cancel_curve(ToolContext& context) {
    if (!m_curve_pending) return;
    m_curve_pending = false;
    m_bending = false;
    hide_preview(context);
}

void ShapeTool::close_polygon(ToolContext& context) {
    if (!m_polygon_open) return;
    const std::vector<Point2f> vertices = m_polygon;
    m_polygon_open = false;
    m_polygon.clear();
    hide_preview(context);
    if (vertices.size() < kMinPolygonPoints) return;

    commit_shape(context, ShapeId::Polygon, m_origin, m_cursor, vertices, m_paint);
}

void ShapeTool::cancel_polygon(ToolContext& context) {
    if (!m_polygon_open) return;
    m_polygon_open = false;
    m_polygon.clear();
    hide_preview(context);
}

void ShapeTool::publish_pending(ToolContext& context) {
    publish_shape(context, m_kind, m_origin, m_end, {}, m_paint, m_rotation);
    ShapePreview& preview = context.ui.shape;
    if (shape::is_closed(m_kind)) {
        preview.frame = shape::normalized_rect(m_origin, m_end);
        preview.rotation = m_rotation;
    } else {
        preview.points = {pixel_of(m_origin), pixel_of(m_end)};
    }
}

void ShapeTool::commit_pending(ToolContext& context) {
    if (!m_pending) return;
    m_pending = false;
    m_edit = Edit::None;
    hide_preview(context);
    commit_shape(context, m_kind, m_origin, m_end, {}, m_paint, m_rotation);
}

void ShapeTool::cancel_pending(ToolContext& context) {
    if (!m_pending) return;
    m_pending = false;
    m_edit = Edit::None;
    hide_preview(context);
}

ShapeTool::Grab ShapeTool::grab_at(const ToolContext& context, Point2f position) const {
    if (m_kind == ShapeId::Line) {
        const std::array ends{m_origin, m_end};
        for (int i = 0; i < static_cast<int>(ends.size()); ++i) {
            // A grip sits in the middle of the pixel the end lands on.
            const Point2f centre = pixel_of(ends[static_cast<std::size_t>(i)]);
            if (distance({centre.x + 0.5f, centre.y + 0.5f}, position) * context.zoom <= kHandleHitRadius)
                return {Edit::Endpoint, std::nullopt, i};
        }
        if (distance_to_segment(position, m_origin, m_end) * context.zoom <= kHandleHitRadius)
            return {Edit::Move, std::nullopt, 0};
        return {};
    }
    // The box is hit-tested as if it were upright: the pointer is turned back
    // by the angle the shape has been turned.
    const RectI box = shape::normalized_rect(m_origin, m_end);
    const Point2f local = shape::turned_about_middle(box, position, -m_rotation);
    if (const std::optional<SelectionHandle> handle = handle_at(box, local, context.zoom))
        return {Edit::Box, handle, 0};
    if (box.contains(local)) return {Edit::Move, std::nullopt, 0};
    if (in_rotate_zone(box, local, context.zoom)) return {Edit::Rotate, std::nullopt, 0};
    return {};
}

bool ShapeTool::grab_pending(ToolContext& context, Point2f position) {
    const Grab grab = grab_at(context, position);
    if (grab.edit == Edit::None) return false;
    m_edit = grab.edit;
    m_press = position;
    m_origin_before = m_origin;
    m_end_before = m_end;
    m_rotation_before = m_rotation;
    m_box_before = shape::normalized_rect(m_origin, m_end);
    m_endpoint = grab.endpoint;
    context.ui.resize = {};
    context.ui.move = {};
    context.ui.rotate = {};
    switch (grab.edit) {
        case Edit::Box:
            m_handle = *grab.handle;
            context.ui.resize.cursor = resize_cursor_for(m_handle);
            break;
        case Edit::Endpoint: context.ui.resize.cursor = line_cursor(m_origin, m_end); break;
        case Edit::Move: context.ui.move = {true, true}; break;
        case Edit::Rotate:
            m_bearing_before = bearing_degrees(middle_of(m_box_before), position);
            context.ui.rotate = {true, true, middle_of(m_box_before)};
            break;
        case Edit::None: break;
    }
    return true;
}

void ShapeTool::edit_pending(ToolContext& context, Point2f position) {
    switch (m_edit) {
        case Edit::Endpoint: (m_endpoint == 0 ? m_origin : m_end) = position; break;
        case Edit::Move: {
            const Point2f offset{std::round(position.x - m_press.x), std::round(position.y - m_press.y)};
            m_origin = {m_origin_before.x + offset.x, m_origin_before.y + offset.y};
            m_end = {m_end_before.x + offset.x, m_end_before.y + offset.y};
            break;
        }
        case Edit::Rotate: {
            const float turn = wrap_degrees(bearing_degrees(middle_of(m_box_before), position) - m_bearing_before);
            float degrees = m_rotation_before + turn;
            // Snap the angle the shape ends up at, not the turn made in this drag.
            if (context.shift) degrees = std::round(degrees / kRotateStepDegrees) * kRotateStepDegrees;
            m_rotation = wrap_degrees(degrees);
            break;
        }
        case Edit::Box: {
            // Every frame starts from the box the drag began with, so the shape
            // never inherits what an earlier frame rounded away. The pointer is
            // turned back into the upright box, and the box that comes out
            // swings around the middle it had, so the corner opposite the grip
            // stays where it was on the paper.
            const Point2f local = shape::turned_about_middle(m_box_before, position, -m_rotation);
            const RectI box = resized_bounds(m_box_before, m_handle, local, kUnboundedBox, context.shift);
            const Point2f middle =
                shape::turned_about_middle(m_box_before, middle_of(box), m_rotation);
            const Point2f origin{std::round(middle.x - static_cast<float>(box.width - 1) / 2.0f),
                                 std::round(middle.y - static_cast<float>(box.height - 1) / 2.0f)};
            m_origin = origin;
            m_end = {origin.x + static_cast<float>(box.width - 1),
                     origin.y + static_cast<float>(box.height - 1)};
            break;
        }
        case Edit::None: break;
    }
    publish_pending(context);
    if (m_edit == Edit::Rotate) context.ui.rotate.centre = middle_of(shape::normalized_rect(m_origin, m_end));
}

void ShapeTool::hover_pending(ToolContext& context, Point2f position) {
    const Grab grab = grab_at(context, position);
    context.ui.resize.cursor = ResizeCursor::None;
    context.ui.move.hover = grab.edit == Edit::Move;
    context.ui.rotate = {grab.edit == Edit::Rotate, false, middle_of(shape::normalized_rect(m_origin, m_end))};
    if (grab.edit == Edit::Box) context.ui.resize.cursor = resize_cursor_for(*grab.handle);
    if (grab.edit == Edit::Endpoint) context.ui.resize.cursor = line_cursor(m_origin, m_end);
}

void ShapeTool::hide_preview(ToolContext& context) {
    ShapePreview& preview = context.ui.shape;
    preview.visible = false;
    preview.frame.reset();
    preview.points.clear();
    context.ui.resize = {};
    context.ui.move = {};
    context.ui.rotate = {};
}

bool ShapeTool::on_cancel(ToolContext& context) {
    // A drag paints nothing until the release - it only publishes a preview -
    // so abandoning it is dropping that preview.
    if (!m_dragging && !m_polygon_open && !m_curve_pending && !m_pending) return false;
    cancel_pending(context);
    cancel_polygon(context);
    cancel_curve(context);
    m_dragging = false;
    hide_preview(context);
    return true;
}

}  // namespace lsp
