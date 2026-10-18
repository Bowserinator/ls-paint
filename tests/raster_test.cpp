#include <cmath>
#include <cstdio>
#include <vector>

#include "core/blur.hpp"
#include "core/warp.hpp"
#include "core/raster.hpp"

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

int painted_count(const PixelBuffer& pixels) {
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) ++count;
    return count;
}

void test_line_is_gap_free_on_a_fast_diagonal() {
    PixelBuffer pixels(48, 8);
    raster::draw_line(Target{pixels}, {0, 0}, {40, 3}, 1, kInk);
    CHECK(painted_count(pixels) == 41);
    for (int x = 0; x <= 40; ++x) {
        bool column_painted = false;
        for (int y = 0; y < pixels.height(); ++y) column_painted = column_painted || pixels.at(x, y).a != 0;
        CHECK(column_painted);
    }
}

void test_line_thickness_stamps_a_square() {
    PixelBuffer pixels(16, 16);
    raster::draw_line(Target{pixels}, {8, 8}, {8, 8}, 4, kInk);
    CHECK(painted_count(pixels) == 16);
    CHECK(pixels.at(7, 7).a == 255);
    CHECK(pixels.at(10, 10).a == 255);
    CHECK(pixels.at(6, 6).a == 0);
    CHECK(pixels.at(11, 11).a == 0);
}

void test_polyline_closes_the_loop() {
    PixelBuffer pixels(16, 16);
    const std::vector<Point2f> triangle{{2, 2}, {12, 2}, {12, 12}};
    raster::draw_polyline(Target{pixels}, triangle, 1, kInk, true);
    CHECK(pixels.at(2, 2).a == 255);
    CHECK(pixels.at(7, 7).a == 255);   // closing edge (12,12) → (2,2)
    CHECK(pixels.at(7, 2).a == 255);
    CHECK(pixels.at(10, 11).a == 0);
}

void test_rect_outline_keeps_the_inside_clear() {
    PixelBuffer pixels(16, 16);
    raster::draw_rect_outline(Target{pixels}, {2, 3, 10, 6}, 1, kInk);
    CHECK(pixels.at(2, 3).a == 255);
    CHECK(pixels.at(11, 8).a == 255);
    CHECK(pixels.at(2, 8).a == 255);
    CHECK(pixels.at(6, 5).a == 0);
}

void test_rect_outline_thickness_does_not_leave_a_seam() {
    PixelBuffer pixels(24, 24);
    raster::draw_rect_outline(Target{pixels}, {2, 2, 20, 20}, 3, kInk);
    for (int y = 2; y < 5; ++y)
        for (int x = 2; x < 22; ++x) CHECK(pixels.at(x, y).a == 255);
    CHECK(pixels.at(11, 11).a == 0);
}

void test_rect_fill_covers_exactly_its_box() {
    PixelBuffer pixels(12, 12);
    raster::draw_rect_fill(Target{pixels}, {1, 2, 3, 4}, kInk);
    CHECK(painted_count(pixels) == 12);
}

void test_rounded_rect_fill_rounds_only_the_corners() {
    PixelBuffer pixels(24, 24);
    raster::draw_rounded_rect_fill(Target{pixels}, {0, 0, 20, 20}, 6, kInk);
    CHECK(pixels.at(0, 0).a == 0);
    CHECK(pixels.at(19, 0).a == 0);
    CHECK(pixels.at(0, 19).a == 0);
    CHECK(pixels.at(6, 10).a == 255);
    CHECK(pixels.at(10, 10).a == 255);
    CHECK(pixels.at(10, 0).a == 255);
}

void test_rounded_rect_outline_is_a_ring() {
    PixelBuffer pixels(24, 24);
    raster::draw_rounded_rect_outline(Target{pixels}, {0, 0, 20, 20}, 6, 2, kInk);
    CHECK(pixels.at(10, 0).a == 255);
    CHECK(pixels.at(10, 10).a == 0);
    CHECK(pixels.at(1, 10).a == 255);
    CHECK(pixels.at(2, 10).a == 0);
}

void test_ellipse_fill_shrinks_toward_the_top_and_bottom() {
    PixelBuffer pixels(12, 8);
    raster::draw_ellipse_fill(Target{pixels}, {0, 0, 8, 4}, kInk);
    for (int x = 0; x < 8; ++x) CHECK(pixels.at(x, 2).a == 255);
    CHECK(pixels.at(0, 0).a == 0);
    CHECK(pixels.at(7, 0).a == 0);
    CHECK(pixels.at(1, 0).a == 255);
    CHECK(pixels.at(6, 0).a == 255);
}

void test_ellipse_outline_leaves_the_middle_of_a_thick_shape_empty() {
    PixelBuffer pixels(28, 20);
    raster::draw_ellipse_outline(Target{pixels}, {4, 4, 20, 12}, 2, kInk);
    CHECK(pixels.at(14, 4).a == 255);
    CHECK(pixels.at(4, 10).a == 255);
    CHECK(pixels.at(5, 10).a == 255);
    CHECK(pixels.at(6, 10).a == 0);
    CHECK(pixels.at(14, 10).a == 0);
}

void test_ellipse_outline_of_a_tiny_shape_fills_it() {
    PixelBuffer pixels(8, 8);
    raster::draw_ellipse_outline(Target{pixels}, {2, 2, 3, 3}, 4, kInk);
    CHECK(pixels.at(3, 3).a == 255);
    CHECK(pixels.at(2, 2).a == 255);
}

void test_polygon_fill_uses_even_odd_for_concave_shapes() {
    PixelBuffer pixels(40, 40);
    std::vector<Point2f> star;
    for (int i = 0; i < 10; ++i) {
        const float radius = i % 2 == 0 ? 12.0f : 5.0f;
        const float angle = static_cast<float>(i) * 3.14159265f / 5.0f - 1.5707963f;
        star.push_back({20.0f + radius * std::cos(angle), 20.0f + radius * std::sin(angle)});
    }
    raster::draw_polygon_fill(Target{pixels}, star, kInk);
    CHECK(pixels.at(20, 20).a == 255);
    // Between two spikes the outline dips to the inner radius, so that point is
    // outside the star even though it is inside its bounding box.
    const float notch_x = 20.0f + 9.0f * std::cos(-1.5707963f + 3.14159265f / 5.0f);
    const float notch_y = 20.0f + 9.0f * std::sin(-1.5707963f + 3.14159265f / 5.0f);
    CHECK(pixels.at(static_cast<int>(notch_x), static_cast<int>(notch_y)).a == 0);
}

void test_polygon_fill_of_a_triangle_covers_half_its_box() {
    PixelBuffer pixels(16, 16);
    const std::vector<Point2f> triangle{{0, 0}, {10, 0}, {0, 10}};
    raster::draw_polygon_fill(Target{pixels}, triangle, kInk);
    CHECK(pixels.at(1, 1).a == 255);
    CHECK(pixels.at(8, 4).a == 0);
    CHECK(pixels.at(5, 5).a == 0);
    const int painted = painted_count(pixels);
    CHECK(painted > 40 && painted < 70);
}

void test_fewer_than_three_polygon_points_paints_nothing() {
    PixelBuffer pixels(8, 8);
    const std::vector<Point2f> line{{1, 1}, {6, 6}};
    raster::draw_polygon_fill(Target{pixels}, line, kInk);
    CHECK(painted_count(pixels) == 0);
}

void test_clip_mask_blocks_pixels_outside_the_selection() {
    PixelBuffer pixels(16, 16);
    std::vector<std::uint8_t> mask(16 * 16, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 8; x < 16; ++x) mask[static_cast<std::size_t>(y) * 16 + x] = 1;
    raster::draw_rect_fill(Target{pixels, mask.data()}, {0, 0, 16, 16}, kInk);
    CHECK(pixels.at(7, 8).a == 0);
    CHECK(pixels.at(8, 8).a == 255);
    CHECK(pixels.at(15, 15).a == 255);
}

void test_glyph_coverage_scales_alpha() {
    PixelBuffer pixels(4, 1);
    const std::uint8_t coverage[4] = {0, 128, 255, 0};
    raster::draw_glyph(Target{pixels}, 0, 0, 4, 1, 4, coverage, {255, 0, 0, 255});
    CHECK(pixels.at(0, 0).a == 0);
    CHECK(pixels.at(1, 0).a >= 120 && pixels.at(1, 0).a <= 136);
    CHECK(pixels.at(1, 0).r == 255);
    CHECK(pixels.at(2, 0).a == 255);
    CHECK(pixels.at(2, 0).r == 255);
    CHECK(pixels.at(3, 0).a == 0);
}

void test_glyph_skips_pixels_off_canvas() {
    PixelBuffer pixels(4, 4);
    const std::uint8_t coverage[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255};
    raster::draw_glyph(Target{pixels}, 2, 2, 4, 4, 4, coverage, {0, 0, 0, 255});
    CHECK(pixels.at(3, 3).a == 255);
    CHECK(pixels.at(0, 0).a == 0);
    CHECK(painted_count(pixels) == 4);
}

void test_bounds_helpers_round_outward() {
    const RectI line = raster::bounds_line({1, 1}, {5, 5}, 3);
    CHECK(line.width == 7 && line.height == 7);
    CHECK(line.x == 0 && line.y == 0);
    const RectI points = raster::bounds_points(std::vector<Point2f>{{2, 3}, {8, 9}}, 1);
    CHECK(points.x <= 2 && points.y <= 3);
    CHECK(points.right() >= 8 && points.bottom() >= 9);
    CHECK(raster::bounds_points(std::vector<Point2f>{}, 1).empty());
    CHECK(raster::bounds_rect({}, 2).empty());
}

void test_target_allows_stays_inside_the_canvas() {
    PixelBuffer pixels(4, 4);
    const Target target{pixels};
    CHECK(target.allows(0, 0));
    CHECK(target.allows(3, 3));
    CHECK(!target.allows(4, 3));
    CHECK(!target.allows(-1, 0));
}

void test_blur_spreads_a_dot() {
    PixelBuffer pixels(9, 9, Rgba8{255, 255, 255, 255});
    pixels.at(4, 4) = kInk;

    const PixelBuffer blurred = blur::box_twice(pixels, 1);

    CHECK(blurred.at(4, 4).r > 0);     // the dot picks up its neighbours
    CHECK(blurred.at(4, 3).r < 255);   // and they pick up the dot
    CHECK(blurred.at(0, 0).r == 255);  // far away nothing moved
}

void test_blur_keeps_a_flat_area_flat() {
    const PixelBuffer pixels(7, 5, Rgba8{40, 90, 160, 255});

    CHECK((blur::box_twice(pixels, 3) == pixels));
}

void test_blur_keeps_a_transparent_area_transparent() {
    const PixelBuffer pixels(6, 6);

    CHECK((blur::box_twice(pixels, 2) == pixels));
}

void test_blur_does_not_blacken_a_soft_edge() {
    // An opaque block on the left half. Blurring thins its edge toward
    // transparency, but the colour must not be dragged toward black on the way.
    PixelBuffer pixels(8, 4, Rgba8{200, 60, 30, 255});
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 4; x < pixels.width(); ++x) pixels.at(x, y) = Rgba8{0, 0, 0, 0};

    const PixelBuffer blurred = blur::box_twice(pixels, 1);

    CHECK(blurred.at(3, 1).a < 200);
    CHECK(blurred.at(3, 1).r >= 198);
    CHECK(blurred.at(3, 1).g <= blurred.at(3, 1).r && blurred.at(3, 1).b <= blurred.at(3, 1).g);
}

void test_blur_ignores_a_radius_below_one() {
    const PixelBuffer pixels(5, 5, Rgba8{10, 20, 30, 200});

    CHECK((blur::box_twice(pixels, 0) == pixels));
    CHECK((blur::box_twice(pixels, -3) == pixels));
}

void test_warp_reads_between_pixels() {
    PixelBuffer pixels(2, 1);
    pixels.at(0, 0) = Rgba8{255, 0, 0, 255};
    pixels.at(1, 0) = Rgba8{0, 0, 255, 255};

    // Halfway between the two centres: an even mix of them.
    const Rgba8 middle = warp::sample_bilinear(pixels, 1.0f, 0.5f);
    CHECK(middle.r == 128);
    CHECK(middle.b == 128);
    CHECK(middle.a == 255);

    // On a pixel's centre the pixel itself comes back.
    CHECK((warp::sample_bilinear(pixels, 0.5f, 0.5f) == pixels.at(0, 0)));
    CHECK((warp::sample_bilinear(pixels, 1.5f, 0.5f) == pixels.at(1, 0)));

    // Outside the picture the nearest edge is held.
    CHECK((warp::sample_bilinear(pixels, -4.0f, 0.5f) == pixels.at(0, 0)));
    CHECK((warp::sample_bilinear(pixels, 99.0f, 0.5f) == pixels.at(1, 0)));
}

void test_warp_does_not_darken_a_soft_edge() {
    PixelBuffer pixels(2, 1);
    pixels.at(0, 0) = Rgba8{255, 255, 255, 255};
    pixels.at(1, 0) = Rgba8{0, 0, 0, 0};  // transparent, so its colour must not count

    const Rgba8 sampled = warp::sample_bilinear(pixels, 1.0f, 0.5f);
    CHECK(sampled.r == 255);
    CHECK(sampled.g == 255);
    CHECK(sampled.b == 255);
    CHECK(sampled.a == 128);
}

}  // namespace

int main() {
    test_line_is_gap_free_on_a_fast_diagonal();
    test_line_thickness_stamps_a_square();
    test_polyline_closes_the_loop();
    test_rect_outline_keeps_the_inside_clear();
    test_rect_outline_thickness_does_not_leave_a_seam();
    test_rect_fill_covers_exactly_its_box();
    test_rounded_rect_fill_rounds_only_the_corners();
    test_rounded_rect_outline_is_a_ring();
    test_ellipse_fill_shrinks_toward_the_top_and_bottom();
    test_ellipse_outline_leaves_the_middle_of_a_thick_shape_empty();
    test_ellipse_outline_of_a_tiny_shape_fills_it();
    test_polygon_fill_uses_even_odd_for_concave_shapes();
    test_polygon_fill_of_a_triangle_covers_half_its_box();
    test_fewer_than_three_polygon_points_paints_nothing();
    test_clip_mask_blocks_pixels_outside_the_selection();
    test_glyph_coverage_scales_alpha();
    test_glyph_skips_pixels_off_canvas();
    test_bounds_helpers_round_outward();
    test_target_allows_stays_inside_the_canvas();
    test_blur_spreads_a_dot();
    test_blur_keeps_a_flat_area_flat();
    test_blur_keeps_a_transparent_area_transparent();
    test_blur_does_not_blacken_a_soft_edge();
    test_blur_ignores_a_radius_below_one();
    test_warp_reads_between_pixels();
    test_warp_does_not_darken_a_soft_edge();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
