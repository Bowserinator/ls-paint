#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "core/color.hpp"

using namespace lsp;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// One step of the 0..255 channel scale, the unit a colour round trip may lose.
constexpr float kUnit = 1.0f / 255.0f;

float hue_gap(float a, float b) {
    const float raw = std::fabs(a - b);
    return std::min(raw, 360.0f - raw);
}

void check_same_color(Rgba8 actual, Rgba8 expected, const char* what) {
    if (actual == expected) return;
    std::printf("FAIL %s: got %d,%d,%d expected %d,%d,%d\n", what, actual.r, actual.g, actual.b, expected.r,
                expected.g, expected.b);
    ++failures;
}

const std::vector<Rgba8> kSamples = {
    {0, 0, 0, 255},     {255, 255, 255, 255}, {255, 0, 0, 255},   {0, 255, 0, 255},   {0, 0, 255, 255},
    {255, 255, 0, 255}, {0, 255, 255, 255},   {255, 0, 255, 255}, {128, 128, 128, 255}, {200, 64, 32, 255},
    {17, 34, 51, 255},  {250, 250, 5, 255},   {5, 250, 250, 255}, {250, 5, 250, 255}, {37, 140, 200, 255},
    {240, 230, 220, 255},
};

void test_rgb_round_trips_through_hsv() {
    for (const Rgba8 sample : kSamples) {
        check_same_color(rgb_from_hsv(hsv_from_rgb(sample)), sample, "rgb->hsv->rgb");
    }
}

void test_hsv_round_trips_through_rgb() {
    // The eight bit channels are the only loss in the trip, so the recovered
    // colour always paints the same bytes back. Hue and saturation only stay
    // within a byte when the colour is far enough from grey for the span to
    // survive rounding, which is what the second half checks.
    for (int hue = 0; hue < 360; hue += 15) {
        for (const float saturation : {1.0f, 0.6f, 0.25f}) {
            for (const float value : {1.0f, 0.8f, 0.4f}) {
                const Hsv original{static_cast<float>(hue), saturation, value};
                const Rgba8 bytes = rgb_from_hsv(original);
                const Hsv back = hsv_from_rgb(bytes);
                check_same_color(rgb_from_hsv(back), bytes, "recovered hsv paints the same colour");
                if (value * saturation < 0.5f) continue;
                CHECK(hue_gap(back.hue, original.hue) <= 1.0f);
                CHECK(std::fabs(back.saturation - original.saturation) <= kUnit);
                CHECK(std::fabs(back.value - original.value) <= kUnit);
            }
        }
    }
}

void test_hue_wraps_around_the_circle() {
    const Rgba8 red{255, 0, 0, 255};
    check_same_color(rgb_from_hsv({0.0f, 1.0f, 1.0f}), red, "hue 0");
    check_same_color(rgb_from_hsv({360.0f, 1.0f, 1.0f}), red, "hue 360");
    check_same_color(rgb_from_hsv({720.0f, 1.0f, 1.0f}), red, "hue 720");
    check_same_color(rgb_from_hsv({-360.0f, 1.0f, 1.0f}), red, "hue -360");
    check_same_color(rgb_from_hsv({-60.0f, 1.0f, 1.0f}), Rgba8{255, 0, 255, 255}, "hue -60");
    check_same_color(rgb_from_hsv({420.0f, 1.0f, 1.0f}), Rgba8{255, 255, 0, 255}, "hue 420");
    check_same_color(rgb_from_hsv({359.0f, 1.0f, 1.0f}), Rgba8{255, 0, 4, 255}, "hue 359");
}

void test_greys_carry_no_saturation() {
    for (const int level : {0, 1, 64, 128, 200, 254, 255}) {
        const Rgba8 grey{static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(level),
                         static_cast<std::uint8_t>(level), 255};
        const Hsv hsv = hsv_from_rgb(grey);
        CHECK(hsv.saturation == 0.0f);
        CHECK(std::fabs(hsv.value - static_cast<float>(level) * kUnit) <= kUnit);
        for (const float hue : {0.0f, 90.0f, 200.0f, 359.0f}) {
            check_same_color(rgb_from_hsv({hue, 0.0f, hsv.value}), grey, "grey from any hue");
        }
    }
}

void test_the_extremes_and_the_primaries() {
    check_same_color(rgb_from_hsv({0.0f, 0.0f, 0.0f}), Rgba8{0, 0, 0, 255}, "black");
    check_same_color(rgb_from_hsv({0.0f, 0.0f, 1.0f}), Rgba8{255, 255, 255, 255}, "white");
    check_same_color(rgb_from_hsv({0.0f, 1.0f, 0.0f}), Rgba8{0, 0, 0, 255}, "value 0 stays black");

    const struct {
        float hue;
        Rgba8 color;
    } primaries[] = {
        {0.0f, {255, 0, 0, 255}},   {60.0f, {255, 255, 0, 255}}, {120.0f, {0, 255, 0, 255}},
        {180.0f, {0, 255, 255, 255}}, {240.0f, {0, 0, 255, 255}}, {300.0f, {255, 0, 255, 255}},
    };
    for (const auto& primary : primaries) {
        check_same_color(rgb_from_hsv({primary.hue, 1.0f, 1.0f}), primary.color, "primary from hue");
        const Hsv back = hsv_from_rgb(primary.color);
        CHECK(hue_gap(back.hue, primary.hue) <= 0.5f);
        CHECK(back.saturation == 1.0f);
        CHECK(back.value == 1.0f);
    }
}

void test_parse_reads_the_written_forms() {
    const Rgba8 red{255, 0, 0, 255};
    const Rgba8 blue{0, 0, 255, 255};
    const struct {
        const char* text;
        Rgba8 color;
    } accepted[] = {
        {"#ff0000", red},      {"#FF0000", red},   {"ff0000", red},  {"FF0000", red},   {"#f00", red},
        {"f00", red},          {" #f00 ", red},    {"255,0,0", red}, {"255, 0, 0", red}, {" 255 , 0 , 0 ", red},
        {"255,0,255", {255, 0, 255, 255}}, {"#0000ff", blue}, {"0000ff", blue},   {"0,0,255", blue},
        {"#000", {0, 0, 0, 255}}, {"255,255,255", {255, 255, 255, 255}},
    };
    for (const auto& entry : accepted) {
        const std::optional<Rgba8> parsed = parse_color(entry.text);
        CHECK(parsed.has_value());
        if (parsed) check_same_color(*parsed, entry.color, entry.text);
    }
}

void test_parse_rejects_what_is_not_a_color() {
    const char* rejected[] = {
        "",        "   ",      "#",      "#ff00",  "#12345", "#ff00zz", "zzz",    "12345g",
        "256,0,0", "-1,0,0",   "1,2",    "1,2,3,4", "1,,3",   ",1,2",    "1,2,",   "1 2 3",
        "1.5,2,3", "0,0,0,0",  "0x10,0,0", "red",   "#ff 000", "255.0,0,0",
    };
    for (const char* text : rejected) {
        const std::optional<Rgba8> parsed = parse_color(text);
        if (parsed) {
            std::printf("FAIL parse accepted \"%s\" as %d,%d,%d\n", text, parsed->r, parsed->g, parsed->b);
            ++failures;
        }
    }
}

void test_hex_text_round_trips() {
    CHECK(to_hex({255, 0, 0, 255}) == "#ff0000");
    CHECK(to_hex({0, 0, 0, 255}) == "#000000");
    CHECK(to_hex({255, 255, 255, 255}) == "#ffffff");
    CHECK(to_hex({171, 205, 239, 255}) == "#abcdef");
    for (const Rgba8 sample : kSamples) {
        const std::optional<Rgba8> parsed = parse_color(to_hex(sample));
        CHECK(parsed.has_value());
        if (parsed) check_same_color(*parsed, sample, "hex round trip");
    }
}

}  // namespace

int main() {
    test_rgb_round_trips_through_hsv();
    test_hsv_round_trips_through_rgb();
    test_hue_wraps_around_the_circle();
    test_greys_carry_no_saturation();
    test_the_extremes_and_the_primaries();
    test_parse_reads_the_written_forms();
    test_parse_rejects_what_is_not_a_color();
    test_hex_text_round_trips();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
