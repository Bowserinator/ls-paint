#include "ui/cursor_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>

#include "utils/math.hpp"

namespace lsp {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kRectCorners = 4;
// The crosshair clears this much room around the pointer, then runs this far.
constexpr float kArmGap = 2.0F;
constexpr float kArmLength = 7.0F;
// Every mark is one screen pixel thick.
constexpr float kMarkThickness = 1.0F;
// The marks are half faded so that the paper still reads through them.
constexpr std::uint8_t kMarkAlpha = 128;
// Outlines are walked in steps no longer than this, so no pixel of them is left
// out. The marks are drawn on whole pixels, the way the paper is drawn.
constexpr float kStep = 0.25F;
// The largest outline the overlay carries. Past this the image would cost more
// than the part of it a window can show.
constexpr int kMaxReach = 1024;

// The box a shape of the given half size covers once it is tilted.
[[nodiscard]] Point2f tilted_half_size(float half_width, float half_height, float degrees) {
    const float radians = radians_from_degrees(degrees);
    const float cosine = std::abs(std::cos(radians));
    const float sine = std::abs(std::sin(radians));
    return {std::hypot(half_width * cosine, half_height * sine),
            std::hypot(half_width * sine, half_height * cosine)};
}

// How far the outline of a tip reaches from the pointer, in screen pixels.
[[nodiscard]] float tip_reach(const CursorTip& tip) {
    const Point2f half_size = tilted_half_size(tip.half_width, tip.half_height, tip.rotation_degrees);
    return std::max(std::abs(tip.offset_x) + half_size.x, std::abs(tip.offset_y) + half_size.y);
}

// Paints marks into the overlay, keeping the colour the paper gives each pixel.
struct Marks {
    PixelBuffer& pixels;
    const PixelBuffer& paper;
    const CursorShape& shape;
    Rgba8 around;
    int reach;

    // Marks the pixel that a position `screen` pixels from the pointer falls on.
    void dot(Point2f screen) {
        const int x = floor_to_int(screen.x) + reach;
        const int y = floor_to_int(screen.y) + reach;
        if (!pixels.contains(x, y)) return;
        Rgba8& pixel = pixels.at(x, y);
        // An outline that doubles back marks a pixel twice: the first wins.
        if (pixel.a >= kMarkAlpha) return;
        pixel = inverted_colour(x, y);
    }

    // Marks the pixels along a straight run.
    void line(Point2f from, Point2f to) {
        const int steps = std::max(1, static_cast<int>(std::ceil(distance(from, to) / kStep)));
        for (int i = 0; i <= steps; ++i) {
            const float along = static_cast<float>(i) / static_cast<float>(steps);
            dot({from.x + (to.x - from.x) * along, from.y + (to.y - from.y) * along});
        }
    }

    // Walks the outline of an ellipse, tilted by `degrees`.
    void ellipse(Point2f centre, float half_width, float half_height, float degrees) {
        const float radians = radians_from_degrees(degrees);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        // A turn of one step moves the point at most a whole radius times it, so
        // the larger half size sets how many turns keep the steps short enough.
        const float turns = 2.0F * kPi * std::max(half_width, half_height) / kStep;
        const int steps = std::clamp(static_cast<int>(std::ceil(turns)), 16, 8192);
        for (int i = 0; i < steps; ++i) {
            const float turn = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(steps);
            const float across = half_width * std::cos(turn);
            const float down = half_height * std::sin(turn);
            dot({centre.x + across * cosine - down * sine,
                 centre.y + across * sine + down * cosine});
        }
    }

    // Walks the four edges of an upright rectangle.
    void rectangle(Point2f centre, float half_width, float half_height) {
        const Point2f corners[kRectCorners] = {{centre.x - half_width, centre.y - half_height},
                                    {centre.x + half_width, centre.y - half_height},
                                    {centre.x + half_width, centre.y + half_height},
                                    {centre.x - half_width, centre.y + half_height}};
        for (int i = 0; i < kRectCorners; ++i) {
            line(corners[i], corners[(i + 1) % kRectCorners]);
        }
    }

    // Walks the four arms, which leave a gap around the pointer.
    void crosshair() {
        line({-kArmGap, 0.0F}, {-kArmGap - kArmLength, 0.0F});
        line({kArmGap, 0.0F}, {kArmGap + kArmLength, 0.0F});
        line({0.0F, -kArmGap}, {0.0F, -kArmGap - kArmLength});
        line({0.0F, kArmGap}, {0.0F, kArmGap + kArmLength});
    }

    // The colour a mark pixel takes: the paper under it, inverted, half faded.
    [[nodiscard]] Rgba8 inverted_colour(int x, int y) const {
        // The centre of the pixel, back in document pixels.
        const float offset_x = static_cast<float>(x - reach) + 0.5F;
        const float offset_y = static_cast<float>(y - reach) + 0.5F;
        const int paper_x = floor_to_int(shape.pointer.x + offset_x / shape.zoom);
        const int paper_y = floor_to_int(shape.pointer.y + offset_y / shape.zoom);
        const Rgba8 under = paper.contains(paper_x, paper_y) ? paper.at(paper_x, paper_y) : around;
        Rgba8 mark = inverted(under);
        mark.a = kMarkAlpha;
        return mark;
    }
};

}  // namespace

CursorOverlay render_cursor_overlay(const PixelBuffer& paper, const CursorShape& shape, Rgba8 around) {
    CursorOverlay overlay;
    if (shape.zoom <= 0.0F) return overlay;

    float reach = 0.0F;
    bool tip = shape.tip.shown;
    if (tip) {
        reach = tip_reach(shape.tip);
        // A tip too large to carry loses its outline but keeps the crosshair.
        tip = reach <= static_cast<float>(kMaxReach);
    }
    if (shape.crosshair) reach = std::max(reach, kArmGap + kArmLength + kMarkThickness);
    if (reach <= 0.0F) return overlay;

    overlay.reach = static_cast<int>(std::ceil(reach));
    overlay.pixels = PixelBuffer(overlay.reach * 2, overlay.reach * 2);
    Marks marks{overlay.pixels, paper, shape, around, overlay.reach};

    if (tip) {
        const Point2f centre{shape.tip.offset_x, shape.tip.offset_y};
        const float half_width = std::max(0.5F, shape.tip.half_width);
        const float half_height = std::max(0.5F, shape.tip.half_height);
        if (shape.tip.square) {
            marks.rectangle(centre, half_width, half_height);
        } else {
            marks.ellipse(centre, half_width, half_height, shape.tip.rotation_degrees);
        }
    }
    if (shape.crosshair) marks.crosshair();
    return overlay;
}

}  // namespace lsp
