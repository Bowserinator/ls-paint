#include <cstddef>
#include <cstdio>
#include <vector>

#include "core/pixel_buffer.hpp"
#include "core/raster.hpp"
#include "tools/text_tool.hpp"

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

TextOptions body_options() {
    TextOptions options;
    options.size = 18.0f;
    return options;
}

int painted_count(const PixelBuffer& pixels) {
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) ++count;
    return count;
}

bool inside(const RectI& box, int x, int y) {
    return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

void test_text_paints_inside_the_bounds_it_reports() {
    PixelBuffer pixels(240, 64);
    const RectI touched = draw_text(Target{pixels}, {4, 4}, "Hello", body_options(), kInk);
    CHECK(!touched.empty());
    CHECK(painted_count(pixels) > 0);
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) CHECK(inside(touched, x, y));
}

void test_the_first_line_starts_at_the_origin() {
    PixelBuffer pixels(120, 48);
    const RectI touched = draw_text(Target{pixels}, {10, 10}, "L", body_options(), kInk);
    CHECK(!touched.empty());
    CHECK(touched.x >= 10);
    CHECK(touched.y >= 10);
}

void test_empty_text_paints_nothing() {
    PixelBuffer pixels(64, 32);
    CHECK(draw_text(Target{pixels}, {2, 2}, "", body_options(), kInk).empty());
    CHECK(painted_count(pixels) == 0);
}

void test_a_newline_starts_a_second_line() {
    PixelBuffer single(240, 96);
    PixelBuffer double_line(240, 96);
    const RectI one = draw_text(Target{single}, {4, 4}, "Ag", body_options(), kInk);
    const RectI two = draw_text(Target{double_line}, {4, 4}, "Ag\nAg", body_options(), kInk);
    CHECK(!one.empty());
    CHECK(two.height > one.height + one.height / 2);
}

void test_larger_text_paints_more_ink() {
    PixelBuffer small(400, 120);
    PixelBuffer large(400, 120);
    TextOptions large_options = body_options();
    large_options.size = 64.0f;
    (void)draw_text(Target{small}, {4, 4}, "Ag", body_options(), kInk);
    (void)draw_text(Target{large}, {4, 4}, "Ag", large_options, kInk);
    CHECK(painted_count(large) > painted_count(small));
}

void test_bold_paints_more_ink_than_regular() {
    PixelBuffer regular(240, 64);
    PixelBuffer bold(240, 64);
    TextOptions bold_options = body_options();
    bold_options.bold = true;
    (void)draw_text(Target{regular}, {4, 4}, "Paint", body_options(), kInk);
    (void)draw_text(Target{bold}, {4, 4}, "Paint", bold_options, kInk);
    CHECK(painted_count(bold) > painted_count(regular));
}

void test_italic_shifts_the_top_of_the_glyphs() {
    PixelBuffer regular(240, 64);
    PixelBuffer italic(240, 64);
    TextOptions italic_options = body_options();
    italic_options.italic = true;
    const RectI plain = draw_text(Target{regular}, {4, 4}, "H", body_options(), kInk);
    const RectI slanted = draw_text(Target{italic}, {4, 4}, "H", italic_options, kInk);
    CHECK(!plain.empty());
    CHECK(!slanted.empty());
    CHECK(slanted != plain);
}

void test_an_unknown_family_falls_back_to_a_bundled_face() {
    PixelBuffer pixels(240, 64);
    TextOptions options = body_options();
    options.family = "No Such Font Anywhere";
    CHECK(!draw_text(Target{pixels}, {4, 4}, "Fallback", options, kInk).empty());
}

void test_a_zero_size_draws_nothing() {
    PixelBuffer pixels(64, 32);
    TextOptions options = body_options();
    options.size = 0.0f;
    CHECK(draw_text(Target{pixels}, {2, 2}, "Gone", options, kInk).empty());
    CHECK(painted_count(pixels) == 0);
}

void test_utf8_is_drawn_and_invalid_bytes_do_not_crash() {
    PixelBuffer accents(240, 64);
    CHECK(!draw_text(Target{accents}, {4, 4}, "caf\xc3\xa9 \xe2\x86\x92", body_options(), kInk).empty());
    PixelBuffer broken(240, 64);
    (void)draw_text(Target{broken}, {4, 4}, "bad \xff\xfe bytes", body_options(), kInk);
}

void test_clip_mask_limits_the_text() {
    PixelBuffer pixels(240, 48);
    std::vector<std::uint8_t> mask(240 * 48, 1);
    for (int y = 0; y < 48; ++y)
        for (int x = 70; x < 240; ++x) mask[static_cast<std::size_t>(y) * 240 + x] = 0;
    (void)draw_text(Target{pixels, mask.data()}, {4, 4}, "Clipped", body_options(), kInk);
    CHECK(painted_count(pixels) > 0);
    for (int y = 0; y < 48; ++y)
        for (int x = 70; x < 240; ++x) CHECK(pixels.at(x, y).a == 0);
}

void test_text_off_canvas_leaves_the_buffer_alone() {
    PixelBuffer pixels(64, 32);
    CHECK(draw_text(Target{pixels}, {200, 200}, "Off", body_options(), kInk).empty());
    CHECK(painted_count(pixels) == 0);
}

}  // namespace

int main() {
    test_text_paints_inside_the_bounds_it_reports();
    test_the_first_line_starts_at_the_origin();
    test_empty_text_paints_nothing();
    test_a_newline_starts_a_second_line();
    test_larger_text_paints_more_ink();
    test_bold_paints_more_ink_than_regular();
    test_italic_shifts_the_top_of_the_glyphs();
    test_an_unknown_family_falls_back_to_a_bundled_face();
    test_a_zero_size_draws_nothing();
    test_utf8_is_drawn_and_invalid_bytes_do_not_crash();
    test_clip_mask_limits_the_text();
    test_text_off_canvas_leaves_the_buffer_alone();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
