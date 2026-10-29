#include "core/raster.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/blend_modes.hpp"
#include "utils/math.hpp"
#include "utils/scanline.hpp"

namespace lsp::raster {

namespace {

// An outline is stamped as a square of `size` pixels, so an even thickness
// leans half a pixel off the mathematical edge. That is the same trade mspaint
// makes, and the reason `bounds_*` round outward.
struct Stamp {
    int offset;
    int size;
};

Stamp stamp_for(int thickness) {
    const int size = std::max(1, thickness);
    return {-(size - 1) / 2, size};
}

void paint_pixel(Target target, int x, int y, Rgba8 color) {
    if (!target.allows(x, y)) return;
    if (color.a == kChannelMax) {
        target.pixels.at(x, y) = color;
        return;
    }
    if (color.a == 0) return;
    target.pixels.at(x, y) = blend_fn_for(BlendMode::Normal)(target.pixels.at(x, y), color, 1.0f);
}

void paint_pixel_with_coverage(Target target, int x, int y, Rgba8 color, float coverage) {
    color.a = static_cast<std::uint8_t>(static_cast<float>(color.a) * coverage + 0.5f);
    paint_pixel(target, x, y, color);
}

void paint_span(Target target, int y, int x0, int x1, Rgba8 color) {
    for (int x = x0; x <= x1; ++x) paint_pixel(target, x, y, color);
}

void stamp(Target target, int cx, int cy, int thickness, Rgba8 color) {
    const Stamp stamp_size = stamp_for(thickness);
    for (int dy = 0; dy < stamp_size.size; ++dy)
        for (int dx = 0; dx < stamp_size.size; ++dx)
            paint_pixel(target, cx + stamp_size.offset + dx, cy + stamp_size.offset + dy, color);
}

struct Ellipse {
    float cx;
    float cy;
    float rx;
    float ry;
};

Ellipse ellipse_in(RectI box) {
    const float rx = static_cast<float>(box.width) / 2.0f;
    const float ry = static_cast<float>(box.height) / 2.0f;
    return {static_cast<float>(box.x) + rx, static_cast<float>(box.y) + ry, rx, ry};
}

// Whether the pixel center (x, y) lies inside `ellipse` shrunk by `inset` on
// every side.
bool inside_ellipse(const Ellipse& ellipse, float x, float y, float inset) {
    const float rx = ellipse.rx - inset;
    const float ry = ellipse.ry - inset;
    if (rx <= 0.0f || ry <= 0.0f) return false;
    const float dx = (x - ellipse.cx) / rx;
    const float dy = (y - ellipse.cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

// Rounded-rectangle membership of a pixel center, shrunk by `inset` on every
// side — the same inset trick the ellipse outline uses.
bool inside_rounded_rect(int x, int y, RectI rect, int radius, int inset) {
    const RectI inner{rect.x + inset, rect.y + inset, rect.width - 2 * inset, rect.height - 2 * inset};
    if (inner.empty()) return false;
    const float px = pixel_center(x);
    const float py = pixel_center(y);
    const float r = std::min({static_cast<float>(radius), inner.width / 2.0f, inner.height / 2.0f});
    // Nearest corner center: the clamped point is the pixel itself on the
    // straight parts of the outline, which are then always inside.
    const float corner_x = std::clamp(px, inner.x + r, inner.right() - r);
    const float corner_y = std::clamp(py, inner.y + r, inner.bottom() - r);
    const float dx = px - corner_x;
    const float dy = py - corner_y;
    return dx * dx + dy * dy <= r * r;
}

// Paints the pixels of `box` whose center satisfies `inside`, clipped to the
// target. The box must already cover the shape plus its thickness.
template <typename InsideFn>
void paint_where(Target target, RectI box, Rgba8 color, InsideFn inside) {
    const RectI area = box.intersected(target.pixels.bounds());
    for (int y = area.y; y < area.bottom(); ++y)
        for (int x = area.x; x < area.right(); ++x)
            if (inside(x, y)) paint_pixel(target, x, y, color);
}

}  // namespace

bool Target::allows(int x, int y) const noexcept {
    if (!pixels.contains(x, y)) return false;
    return clip == nullptr || clip->contains(x, y);
}

void draw_line(Target target, Point2f a, Point2f b, int thickness, Rgba8 color) {
    int x0 = round_to_int(a.x);
    int y0 = round_to_int(a.y);
    const int x1 = round_to_int(b.x);
    const int y1 = round_to_int(b.y);

    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        stamp(target, x0, y0, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (twice <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void draw_rect_fill(Target target, RectI rect, Rgba8 color) {
    if (rect.empty()) return;
    for (int y = rect.y; y < rect.bottom(); ++y) paint_span(target, y, rect.x, rect.right() - 1, color);
}

void draw_rect_outline(Target target, RectI rect, int thickness, Rgba8 color) {
    const int t = std::max(1, thickness);
    if (rect.empty()) return;
    if (t * 2 >= std::min(rect.width, rect.height)) {
        draw_rect_fill(target, rect, color);
        return;
    }
    for (int i = 0; i < t; ++i) {
        paint_span(target, rect.y + i, rect.x, rect.right() - 1, color);
        paint_span(target, rect.bottom() - 1 - i, rect.x, rect.right() - 1, color);
        for (int y = rect.y; y < rect.bottom(); ++y) {
            paint_pixel(target, rect.x + i, y, color);
            paint_pixel(target, rect.right() - 1 - i, y, color);
        }
    }
}

void draw_rounded_rect_fill(Target target, RectI rect, int radius, Rgba8 color) {
    if (rect.empty()) return;
    paint_where(target, rect, color,
                [&](int x, int y) { return inside_rounded_rect(x, y, rect, radius, 0); });
}

void draw_rounded_rect_outline(Target target, RectI rect, int radius, int thickness, Rgba8 color) {
    if (rect.empty()) return;
    const int t = std::max(1, thickness);
    if (t * 2 >= std::min(rect.width, rect.height)) {
        draw_rounded_rect_fill(target, rect, radius, color);
        return;
    }
    // The outline is the ring between the shape and the same shape inset by the
    // thickness, so thick corners stay concentric.
    paint_where(target, bounds_rect(rect, t), color, [&](int x, int y) {
        return inside_rounded_rect(x, y, rect, radius, 0) &&
               !inside_rounded_rect(x, y, rect, radius, t);
    });
}

void draw_ellipse_fill(Target target, RectI box, Rgba8 color) {
    if (box.empty()) return;
    const Ellipse ellipse = ellipse_in(box);
    paint_where(target, box, color, [&](int x, int y) {
        return inside_ellipse(ellipse, pixel_center(x), pixel_center(y), 0.0f);
    });
}

void draw_ellipse_outline(Target target, RectI box, int thickness, Rgba8 color) {
    if (box.empty()) return;
    const int t = std::max(1, thickness);
    const Ellipse ellipse = ellipse_in(box);
    paint_where(target, bounds_rect(box, t), color, [&](int x, int y) {
        const float px = pixel_center(x);
        const float py = pixel_center(y);
        return inside_ellipse(ellipse, px, py, 0.0f) &&
               !inside_ellipse(ellipse, px, py, static_cast<float>(t));
    });
}

void draw_polyline(Target target, std::span<const Point2f> points, int thickness, Rgba8 color,
                   bool closed) {
    if (points.empty()) return;
    if (points.size() == 1) {
        stamp(target, round_to_int(points[0].x), round_to_int(points[0].y), thickness, color);
        return;
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        draw_line(target, points[i], points[i + 1], thickness, color);
    if (closed) draw_line(target, points.back(), points.front(), thickness, color);
}

void draw_polygon_fill(Target target, std::span<const Point2f> points, Rgba8 color) {
    if (points.size() < kMinPolygonPoints) return;
    const RectI box = bounds_points(points, 0).intersected(target.pixels.bounds());
    std::vector<float> crossings;
    for (int y = box.y; y < box.bottom(); ++y) {
        const float scan = pixel_center(y);
        polygon_crossings(points, scan, crossings);
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int x0 = ceil_to_int(crossings[i] - 0.5f);
            const int x1 = floor_to_int(crossings[i + 1] - 0.5f);
            if (x0 <= x1) paint_span(target, y, x0, x1, color);
        }
    }
}

void draw_glyph(Target target, int x, int y, int width, int height, int stride,
                const std::uint8_t* coverage, Rgba8 color) {
    if (coverage == nullptr || width <= 0 || height <= 0) return;
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* line = coverage + static_cast<std::size_t>(row) * stride;
        for (int column = 0; column < width; ++column) {
            if (line[column] == 0) continue;
            paint_pixel_with_coverage(target, x + column, y + row, color, unit_from_byte(line[column]));
        }
    }
}

RectI bounds_rect(RectI rect, int thickness) {
    if (rect.empty()) return {};
    const int grow = std::max(0, thickness) / 2;
    return {rect.x - grow, rect.y - grow, rect.width + 2 * grow, rect.height + 2 * grow};
}

RectI bounds_points(std::span<const Point2f> points, int thickness) {
    if (points.empty()) return {};
    float min_x = points[0].x;
    float min_y = points[0].y;
    float max_x = min_x;
    float max_y = min_y;
    for (const Point2f& point : points) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }
    const int left = floor_to_int(min_x);
    const int top = floor_to_int(min_y);
    return bounds_rect({left, top, ceil_to_int(max_x) - left + 1, ceil_to_int(max_y) - top + 1}, thickness);
}

}  // namespace lsp::raster
