// Geometry rules every shape kind has to obey: an outline that never crosses
// itself, an interior that fills without leaving a gap behind, and a rounded
// heart rather than a stretched one. A kind whose outline crosses itself paints
// a hole or a seam because the scanline fill counts the overlap twice.

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "core/raster.hpp"
#include "tools/shape_tool.hpp"

using namespace lsp;
using lsp::raster::Target;

namespace {

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
    const ShapeOptions options{kind, ShapeMode::Fill, 3, 8};
    rasterize_shape(Target{pixels}, kind, {4, 4}, {static_cast<float>(width + 3),
                                                   static_cast<float>(height + 3)},
                    clicked_polygon(), options, kInk, kFill);
    if (painted_count(pixels) == 0) std::printf("kind %d painted nothing\n", static_cast<int>(kind));
    CHECK(painted_count(pixels) > 0);
    if (encloses_background(pixels))
        std::printf("kind %d at %dx%d walled off background\n", static_cast<int>(kind), width, height);
    CHECK(!encloses_background(pixels));
}

void test_fill_covers_more_than_the_outline(const ShapeId kind) {
    PixelBuffer outline(88, 88);
    PixelBuffer filled(88, 88);
    ShapeOptions options{kind, ShapeMode::Outline, 3, 8};
    rasterize_shape(Target{outline}, kind, {4, 4}, {83, 83}, clicked_polygon(), options, kInk, kFill);
    options.mode = ShapeMode::Fill;
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
    const ShapeOptions options{ShapeId::Lightning, ShapeMode::Fill, 3, 8};
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

}  // namespace

int main() {
    test_every_kind_is_reachable();
    test_outlines_do_not_cross_themselves();
    test_heart_is_wide_and_centred();
    test_lightning_fill_is_solid();
    test_rectangle_and_oval_use_their_own_primitives();

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
