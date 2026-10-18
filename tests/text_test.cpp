#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/pixel_buffer.hpp"
#include "core/raster.hpp"
#include "text/font_catalog.hpp"
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

// The lowest row that carries any ink, or -1 when the buffer is blank.
int last_ink_row(const PixelBuffer& pixels) {
    for (int y = pixels.height() - 1; y >= 0; --y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) return y;
    return -1;
}

// The most ink one row carries. A rule reads as a long run here, longer than
// any row of glyphs.
int widest_row(const PixelBuffer& pixels) {
    int widest = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        int covered = 0;
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y).a != 0) ++covered;
        widest = std::max(widest, covered);
    }
    return widest;
}

int count_color(const PixelBuffer& pixels, Rgba8 color) {
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            if (pixels.at(x, y) == color) ++count;
    return count;
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

void test_underline_adds_a_rule_below_the_text() {
    PixelBuffer plain(240, 64);
    PixelBuffer ruled(240, 64);
    TextOptions options = body_options();
    options.underline = true;
    const RectI bare = draw_text(Target{plain}, {4, 4}, "Hello", body_options(), kInk);
    const RectI line = draw_text(Target{ruled}, {4, 4}, "Hello", options, kInk);
    CHECK(!bare.empty());
    CHECK(!line.empty());
    // The rule sits under the baseline, so the block reaches lower than the
    // glyphs do and carries a run no row of glyphs has.
    CHECK(last_ink_row(ruled) > last_ink_row(plain));
    CHECK(line.height > bare.height);
    CHECK(widest_row(ruled) > widest_row(plain));
}

void test_strikethrough_adds_a_rule_across_the_text() {
    PixelBuffer plain(240, 64);
    PixelBuffer struck(240, 64);
    TextOptions options = body_options();
    options.strikethrough = true;
    (void)draw_text(Target{plain}, {4, 4}, "Hello", body_options(), kInk);
    const RectI bar = draw_text(Target{struck}, {4, 4}, "Hello", options, kInk);
    CHECK(!bar.empty());
    // The bar crosses the glyphs, so it stays inside the height they already
    // reach.
    CHECK(last_ink_row(struck) == last_ink_row(plain));
    CHECK(widest_row(struck) > widest_row(plain));
}

void test_an_opaque_background_fills_the_line_band() {
    const Rgba8 paper{200, 40, 40, 255};
    PixelBuffer plain(240, 64);
    PixelBuffer filled(240, 64);
    TextOptions options = body_options();
    options.opaque_background = true;
    options.background = paper;
    (void)draw_text(Target{plain}, {4, 4}, "Hello", body_options(), kInk);
    (void)draw_text(Target{filled}, {4, 4}, "Hello", options, kInk);
    CHECK(count_color(plain, paper) == 0);
    // The band is the whole line box, so it covers more than the ink it holds.
    CHECK(count_color(filled, paper) > widest_row(plain));
    CHECK(widest_row(filled) > widest_row(plain));
    // The glyphs still paint over the band.
    CHECK(count_color(filled, kInk) > 0);
}

void test_an_unknown_family_falls_back_to_a_bundled_face() {
    PixelBuffer pixels(240, 64);
    TextOptions options = body_options();
    options.family = "No Such Font Anywhere";
    CHECK(!draw_text(Target{pixels}, {4, 4}, "Fallback", options, kInk).empty());
}

void test_the_catalog_lists_installed_families_with_files() {
    const std::vector<FontFace> faces = installed_fonts().faces();
    CHECK(!faces.empty());
    const auto stored = std::ranges::find_if(
        faces, [](const FontFace& face) { return !face.path.empty(); });
    CHECK(stored != faces.end());
    if (stored != faces.end()) {
        const std::string path = installed_fonts().face_path(stored->family);
        CHECK(!path.empty());
        CHECK(std::filesystem::exists(path));
    }

    const std::vector<std::string> families = text_font_families();
    CHECK(!families.empty());
    CHECK(std::ranges::find(families, std::string{"DejaVu Sans"}) != families.end());
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
    SelectionMask mask(240, 48);
    mask.fill_rect({0, 0, 70, 48});
    (void)draw_text(Target{pixels, &mask}, {4, 4}, "Clipped", body_options(), kInk);
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
    test_underline_adds_a_rule_below_the_text();
    test_strikethrough_adds_a_rule_across_the_text();
    test_an_opaque_background_fills_the_line_band();
    test_an_unknown_family_falls_back_to_a_bundled_face();
    test_the_catalog_lists_installed_families_with_files();
    test_a_zero_size_draws_nothing();
    test_utf8_is_drawn_and_invalid_bytes_do_not_crash();
    test_clip_mask_limits_the_text();
    test_text_off_canvas_leaves_the_buffer_alone();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
