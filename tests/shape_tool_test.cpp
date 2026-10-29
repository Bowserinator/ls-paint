// Geometry rules every shape kind has to obey: an outline that never crosses
// itself, an interior that fills without leaving a gap behind, and a rounded
// heart rather than a stretched one. A kind whose outline crosses itself paints
// a hole or a seam because the scanline fill counts the overlap twice.

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "core/raster.hpp"
#include "tools/shape_tool.hpp"

using namespace lsp;
using lsp::raster::Target;

namespace {

constexpr std::array<ShapeId, 23> kShapeIds{
    ShapeId::Line,         ShapeId::Curve,         ShapeId::Oval,       ShapeId::Rectangle,
    ShapeId::RoundedRect,  ShapeId::Polygon,       ShapeId::Triangle,   ShapeId::RightTriangle,
    ShapeId::Diamond,      ShapeId::Pentagon,      ShapeId::Hexagon,    ShapeId::ArrowRight,
    ShapeId::ArrowLeft,    ShapeId::ArrowUp,       ShapeId::ArrowDown,  ShapeId::Star4,
    ShapeId::Star5,        ShapeId::Star6,         ShapeId::CalloutRounded, ShapeId::CalloutOval,
    ShapeId::CalloutCloud, ShapeId::Heart,         ShapeId::Lightning,
};

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

const Rgba8 kInk{0, 0, 0, 255};
const Rgba8 kFill{120, 120, 255, 255};

// The vertices of a shape the user builds click by click, used for the one kind
// whose outline comes from the pointer rather than the drag box.
std::vector<Point2f> clicked_polygon() {
    return {{2, 10}, {30, 2}, {44, 26}, {22, 40}, {6, 30}};
}

int painted_count(const PixelBuffer& pixels) {
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) ++count;
    return count;
}

int painted_in(const PixelBuffer& pixels, RectI region) {
    int count = 0;
    for (int y = region.y; y < region.bottom(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            if (pixels.contains(x, y) && pixels.at(x, y).a != 0) ++count;
    return count;
}

// How many different pixel values a drawing uses: a line of one colour has one,
// a brush texture has several.
int distinct_values(const PixelBuffer& pixels) {
    std::vector<Rgba8> seen;
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
            const Rgba8 pixel = pixels.at(x, y);
            if (pixel.a == 0) continue;
            if (std::find(seen.begin(), seen.end(), pixel) == seen.end()) seen.push_back(pixel);
        }
    }
    return static_cast<int>(seen.size());
}

bool same_pixels(const PixelBuffer& a, const PixelBuffer& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.at(x, y) != b.at(x, y)) return false;
    return true;
}

// True when the two segments cross away from a shared endpoint.
bool segments_cross(Point2f a, Point2f b, Point2f c, Point2f d) {
    const auto side = [](Point2f p, Point2f q, Point2f r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    const float first = side(a, b, c);
    const float second = side(a, b, d);
    const float third = side(c, d, a);
    const float fourth = side(c, d, b);
    return ((first > 0) != (second > 0)) && ((third > 0) != (fourth > 0));
}

// A point outside the shape that the border flood fill cannot reach is walled
// in: the fill left a gap the shape surrounds.
bool encloses_background(const PixelBuffer& pixels) {
    std::vector<bool> seen(static_cast<std::size_t>(pixels.width()) * pixels.height(), false);
    std::vector<std::pair<int, int>> pending;
    const auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height()) return;
        const std::size_t index = static_cast<std::size_t>(y) * pixels.width() + x;
        if (seen[index] || pixels.at(x, y).a != 0) return;
        seen[index] = true;
        pending.emplace_back(x, y);
    };
    for (int x = 0; x < pixels.width(); ++x) {
        push(x, 0);
        push(x, pixels.height() - 1);
    }
    for (int y = 0; y < pixels.height(); ++y) {
        push(0, y);
        push(pixels.width() - 1, y);
    }
    while (!pending.empty()) {
        const auto [x, y] = pending.back();
        pending.pop_back();
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * pixels.width() + x;
            if (!seen[index] && pixels.at(x, y).a == 0) return true;
        }
    return false;
}

void test_every_kind_is_reachable() {
    CHECK(kShapeIds.size() == 23);
    for (const ShapeId kind : kShapeIds) {
        const std::vector<Point2f> points = shape::outline(kind, {0, 0}, {47, 39}, clicked_polygon());
        // No kind is allowed to draw nothing at all from a plain drag.
        CHECK(!points.empty());
    }
}

bool same_point(Point2f a, Point2f b) { return lsp::distance(a, b) < 0.01f; }

void test_outlines_do_not_cross_themselves() {
    for (const ShapeId kind : kShapeIds) {
        if (!shape::is_closed(kind)) continue;
        const std::vector<Point2f> points = shape::outline(kind, {0, 0}, {47, 39}, clicked_polygon());
        int crossings = 0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Point2f a = points[i];
            const Point2f b = points[(i + 1) % points.size()];
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                const Point2f c = points[j];
                const Point2f d = points[(j + 1) % points.size()];
                // Edges that share a vertex meet by design, including the pair
                // that closes a sampled loop back onto its first point.
                if (same_point(a, c) || same_point(a, d) || same_point(b, c) || same_point(b, d))
                    continue;
                if (segments_cross(a, b, c, d)) {
                    ++crossings;
                }
            }
        }
        if (crossings != 0)
            std::printf("kind %d has %d crossing edges\n", static_cast<int>(kind), crossings);
        CHECK(crossings == 0);
    }
}

void test_filled_shapes_leave_no_gap(const ShapeId kind, int width, int height) {
    PixelBuffer pixels(width + 8, height + 8);
    const ShapeOptions options{kind, ShapeTexture::Solid, ShapeTexture::Solid, 3, 8};
    rasterize_shape(Target{pixels}, kind, {4, 4}, {static_cast<float>(width + 3),
                                                   static_cast<float>(height + 3)},
                    clicked_polygon(), options, kInk, kFill);
    if (painted_count(pixels) == 0) std::printf("kind %d painted nothing\n", static_cast<int>(kind));
    CHECK(painted_count(pixels) > 0);
    if (encloses_background(pixels))
        std::printf("kind %d at %dx%d walled off background\n", static_cast<int>(kind), width, height);
    CHECK(!encloses_background(pixels));
}

// A background pixel the fill leaves behind on one side and the outline on the
// other, with every neighbour painted, is a line the user sees running through
// the shape.
int fill_outline_seams(const PixelBuffer& pixels, Rgba8 fill, Rgba8 outline) {
    const auto painted = [&](int x, int y) { return pixels.at(x, y).a != 0; };
    int seams = 0;
    for (int y = 1; y + 1 < pixels.height(); ++y)
        for (int x = 1; x + 1 < pixels.width(); ++x) {
            if (painted(x, y)) continue;
            if (!painted(x - 1, y) || !painted(x + 1, y) || !painted(x, y - 1) ||
                !painted(x, y + 1))
                continue;
            bool fill_left = false, outline_left = false, fill_right = false, outline_right = false;
            for (int d = 1; d <= 2; ++d) {
                if (x - d >= 0) {
                    fill_left = fill_left || pixels.at(x - d, y) == fill;
                    outline_left = outline_left || pixels.at(x - d, y) == outline;
                }
                if (x + d < pixels.width()) {
                    fill_right = fill_right || pixels.at(x + d, y) == fill;
                    outline_right = outline_right || pixels.at(x + d, y) == outline;
                }
            }
            if ((fill_left && outline_right) || (outline_left && fill_right)) ++seams;
        }
    return seams;
}

void test_fill_meets_the_outline_at_thin_thickness() {
    // A one or two pixel stroke follows the pixel grid, so on a diagonal edge it
    // used to sit a pixel away from the fill and leave a line of background.
    for (const ShapeId kind : kShapeIds) {
        if (!shape::is_closed(kind)) continue;
        for (const int thickness : {1, 2}) {
            PixelBuffer pixels(88, 88);
            const ShapeOptions options{kind, ShapeTexture::Solid, ShapeTexture::Solid, thickness, 8};
            rasterize_shape(Target{pixels}, kind, {4, 4}, {83, 83}, clicked_polygon(), options, kInk,
                            kFill);
            const int seams = fill_outline_seams(pixels, kFill, kInk);
            if (seams != 0)
                std::printf("kind %d thickness %d leaves %d seam pixels\n", static_cast<int>(kind),
                            thickness, seams);
            CHECK(seams == 0);
        }
    }
}

void test_fill_covers_more_than_the_outline(const ShapeId kind) {
    PixelBuffer outline(88, 88);
    PixelBuffer filled(88, 88);
    ShapeOptions options{kind, ShapeTexture::Solid, ShapeTexture::None, 3, 8};
    rasterize_shape(Target{outline}, kind, {4, 4}, {83, 83}, clicked_polygon(), options, kInk, kFill);
    options.fill = ShapeTexture::Solid;
    rasterize_shape(Target{filled}, kind, {4, 4}, {83, 83}, clicked_polygon(), options, kInk, kFill);
    CHECK(painted_count(outline) > 0);
    CHECK(painted_count(filled) > painted_count(outline));
}

RectI outline_box(const ShapeId kind, Point2f a, Point2f b) {
    RectI box{};
    bool first = true;
    for (const Point2f& point : shape::outline(kind, a, b, clicked_polygon())) {
        const int x = static_cast<int>(std::lround(point.x));
        const int y = static_cast<int>(std::lround(point.y));
        if (first) {
            box = {x, y, 1, 1};
            first = false;
            continue;
        }
        const int left = std::min(box.x, x);
        const int top = std::min(box.y, y);
        box = {left, top, std::max(box.right(), x + 1) - left, std::max(box.bottom(), y + 1) - top};
    }
    return box;
}

void test_heart_is_wide_and_centred() {
    const RectI box = outline_box(ShapeId::Heart, {0, 0}, {79, 79});
    // The curve is wider than it is tall, so a square drag must not stand it up.
    CHECK(box.width > box.height);
    CHECK(box.width <= 80);
    CHECK(box.height <= 80);
    CHECK(std::abs((box.y + box.height / 2) - 40) <= 1);
    CHECK(std::abs((box.x + box.width / 2) - 40) <= 1);
}

void test_lightning_fill_is_solid() {
    // The bolt is the kind that used to fill along a single edge because its
    // outline crossed itself near the waist.
    PixelBuffer pixels(88, 88);
    const ShapeOptions options{ShapeId::Lightning, ShapeTexture::Solid, ShapeTexture::Solid, 3, 8};
    rasterize_shape(Target{pixels}, ShapeId::Lightning, {4, 4}, {83, 83}, {}, options, kInk, kFill);
    CHECK(painted_count(pixels) > 1000);
    CHECK(!encloses_background(pixels));
}

void test_rectangle_and_oval_use_their_own_primitives() {
    // The three box kinds go through the dedicated raster primitives, so their
    // outlines have to stay inside the drag box while the sampled kinds fill it.
    const RectI box = outline_box(ShapeId::Rectangle, {0, 0}, {47, 39});
    CHECK(box.x == 0);
    CHECK(box.y == 0);
    CHECK(box.width == 48);
    CHECK(box.height == 40);
}

// A textured outline stamps a brush along the path instead of drawing a hard
// line: it differs from the solid outline, paints more than one value because
// of the profile's grain and soft edge, and stays close to the path itself.
void test_textured_outlines_are_grainy_and_bounded() {
    const ShapeTexture textures[] = {ShapeTexture::Crayon, ShapeTexture::Marker, ShapeTexture::Oil,
                                     ShapeTexture::NaturalPencil, ShapeTexture::Watercolor};
    for (const ShapeId kind :
         {ShapeId::Rectangle, ShapeId::Oval, ShapeId::Lightning, ShapeId::Star5}) {
        PixelBuffer solid(88, 88);
        ShapeOptions options{kind, ShapeTexture::Solid, ShapeTexture::None, 3, 8};
        rasterize_shape(Target{solid}, kind, {4, 4}, {83, 83}, {}, options, kInk, kFill);
        CHECK(painted_count(solid) > 0);
        for (const ShapeTexture texture : textures) {
            PixelBuffer pixels(88, 88);
            options.outline = texture;
            rasterize_shape(Target{pixels}, kind, {4, 4}, {83, 83}, {}, options, kInk, kFill);
            if (painted_count(pixels) == 0)
                std::printf("kind %d texture %d painted nothing\n", static_cast<int>(kind),
                            static_cast<int>(texture));
            CHECK(painted_count(pixels) > 0);
            CHECK(!same_pixels(solid, pixels));
            if (distinct_values(pixels) < 2)
                std::printf("kind %d texture %d is a flat line\n", static_cast<int>(kind),
                            static_cast<int>(texture));
            CHECK(distinct_values(pixels) >= 2);
            // The drag box spans 4..83; a nib of three pixels reaches two past it.
            CHECK(painted_in(pixels, RectI{0, 0, 88, 2}) == 0);
            CHECK(painted_in(pixels, RectI{0, 86, 88, 2}) == 0);
            CHECK(painted_in(pixels, RectI{0, 0, 2, 88}) == 0);
            CHECK(painted_in(pixels, RectI{86, 0, 2, 88}) == 0);
        }
    }
}

// The selection the raster target carries holds the stamp back as well, so a
// textured part cannot write outside the marching ants.
void test_textured_outline_respects_the_target_clip() {
    SelectionMask clip(88, 88);
    clip.fill_rect({44, 0, 44, 88});
    PixelBuffer pixels(88, 88);
    const ShapeOptions options{ShapeId::Oval, ShapeTexture::Crayon, ShapeTexture::None, 3, 8};
    rasterize_shape(Target{pixels, &clip}, ShapeId::Oval, {4, 4}, {83, 83}, {}, options,
                    kInk, kFill);
    CHECK(painted_count(pixels) > 0);
    CHECK(painted_in(pixels, RectI{0, 0, 44, 88}) == 0);
}

// A point is inside a closed polygon when a ray to the left crosses its
// edges an odd number of times.
bool inside_polygon(const std::vector<Point2f>& points, float x, float y) {
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const Point2f& a = points[i];
        const Point2f& b = points[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

// Distance from a point to the polygon's outline, used to allow for the pixel
// that a nib straddles where an edge falls between two pixel centres.
float distance_to_outline(const std::vector<Point2f>& points, float x, float y) {
    float best = 1.0e9f;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const Point2f& a = points[i];
        const Point2f& b = points[j];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length_squared = dx * dx + dy * dy;
        float t = 0.0f;
        if (length_squared > 0.0f) t = ((x - a.x) * dx + (y - a.y) * dy) / length_squared;
        t = std::clamp(t, 0.0f, 1.0f);
        const float px = a.x + t * dx - x;
        const float py = a.y + t * dy - y;
        best = std::min(best, std::sqrt(px * px + py * py));
    }
    return best;
}

// A textured fill covers the shape's inside with the texture's grain and stays
// inside the shape; with an outline chosen as well, both parts are painted.
void test_textured_fills_are_grainy_and_bounded() {
    const ShapeTexture textures[] = {ShapeTexture::Crayon, ShapeTexture::Marker, ShapeTexture::Oil,
                                     ShapeTexture::NaturalPencil, ShapeTexture::Watercolor};
    for (const ShapeId kind :
         {ShapeId::Rectangle, ShapeId::Oval, ShapeId::Lightning, ShapeId::Star5}) {
        const std::vector<Point2f> points = shape::outline(kind, {4, 4}, {83, 83}, {});
        CHECK(points.size() >= 3);
        for (const ShapeTexture texture : textures) {
            PixelBuffer fill_only(88, 88);
            ShapeOptions options{kind, ShapeTexture::None, texture, 1, 8};
            rasterize_shape(Target{fill_only}, kind, {4, 4}, {83, 83}, {}, options, kInk, kFill);
            if (painted_count(fill_only) == 0)
                std::printf("kind %d texture %d filled nothing\n", static_cast<int>(kind),
                            static_cast<int>(texture));
            CHECK(painted_count(fill_only) > 0);
            if (distinct_values(fill_only) < 2)
                std::printf("kind %d texture %d is flat\n", static_cast<int>(kind),
                            static_cast<int>(texture));
            CHECK(distinct_values(fill_only) >= 2);
            int outside = 0;
            for (int y = 0; y < 88; ++y)
                for (int x = 0; x < 88; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    if (fill_only.at(x, y).a != 0 && !inside_polygon(points, px, py) &&
                        distance_to_outline(points, px, py) > 1.5f)
                        ++outside;
                }
            if (outside != 0)
                std::printf("kind %d texture %d painted %d pixels outside\n",
                            static_cast<int>(kind), static_cast<int>(texture), outside);
            CHECK(outside == 0);
            // The outline is painted as well and never sits inside the fill.
            PixelBuffer both(88, 88);
            options.outline = ShapeTexture::Solid;
            rasterize_shape(Target{both}, kind, {4, 4}, {83, 83}, {}, options, kInk, kFill);
            CHECK(painted_count(both) > painted_count(fill_only));
        }
    }
    // A hatched interior is painted, not left empty.
    PixelBuffer hatched(88, 88);
    const ShapeOptions options{ShapeId::Rectangle, ShapeTexture::Solid, ShapeTexture::Crayon, 1, 8};
    rasterize_shape(Target{hatched}, ShapeId::Rectangle, {4, 4}, {83, 83}, {}, options, kInk, kFill);
    CHECK(painted_in(hatched, RectI{10, 10, 60, 60}) > 200);
}

}  // namespace

int main() {
    test_every_kind_is_reachable();
    test_outlines_do_not_cross_themselves();
    test_fill_meets_the_outline_at_thin_thickness();
    test_heart_is_wide_and_centred();
    test_lightning_fill_is_solid();
    test_rectangle_and_oval_use_their_own_primitives();
    test_textured_outlines_are_grainy_and_bounded();
    test_textured_outline_respects_the_target_clip();
    test_textured_fills_are_grainy_and_bounded();

    for (const ShapeId kind : kShapeIds) {
        if (!shape::is_closed(kind)) continue;
        test_fill_covers_more_than_the_outline(kind);
        test_filled_shapes_leave_no_gap(kind, 80, 80);
        test_filled_shapes_leave_no_gap(kind, 40, 40);
        test_filled_shapes_leave_no_gap(kind, 24, 24);
        test_filled_shapes_leave_no_gap(kind, 80, 40);
    }

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
