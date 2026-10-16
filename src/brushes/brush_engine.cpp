#include "brushes/brush_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/blend_modes.hpp"

namespace lsp {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// A fixed value in 0..1 per pixel, so a grainy brush always looks the same.
float grain_at(int x, int y) {
    std::uint32_t hash =
        static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return static_cast<float>(hash & 0xFFFFu) / 65535.0f;
}

// How much ink the tip puts on one pixel: full in the solid middle, fading to
// nothing over the soft rim.
float ink_at(const BrushProfile& profile, float dx, float dy, float radius, int x, int y) {
    if (radius <= 0.0f) return profile.alpha;
    // Distance from the middle, measured along and across the tip so that a
    // slanted chisel nib covers the pixels its angle says it should.
    const float radians = profile.angle * kPi / 180.0f;
    const float along = dx * std::cos(radians) + dy * std::sin(radians);
    const float across = -dx * std::sin(radians) + dy * std::cos(radians);
    const float reach =
        profile.shape == BrushShape::Square
            ? std::max(std::abs(along), std::abs(across) / profile.nib_width) / radius
            : std::hypot(along, across / profile.nib_width) / radius;
    if (reach > 1.0f) return 0.0f;
    const float solid = std::clamp(profile.coverage, 0.0f, 1.0f);
    const float edge = reach <= solid ? 1.0f : 1.0f - (reach - solid) / (1.0f - solid);
    const float grain =
        profile.grain > 0.0f ? 1.0f - profile.grain * (1.0f - grain_at(x, y)) : 1.0f;
    return profile.alpha * edge * grain;
}

}  // namespace

BrushProfile BrushEngine::effective_profile(const Brush& brush) {
    BrushProfile profile = brush::profile(brush.kind);
    if (brush.kind == BrushKind::Brush) {
        profile.shape = brush.shape;
        profile.spacing = brush.spacing;
    }
    return profile;
}

RectI BrushEngine::stroke_to(PixelBuffer& target, Point2f to) {
    const float step = std::max(1.0f, m_profile.spacing * static_cast<float>(m_brush.size));

    if (!m_last) {
        m_last = to;
        m_carry = step;
        return stamp(target, to);
    }

    const Point2f from = *m_last;
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);

    RectI dirty;
    float travelled = m_carry;
    for (; travelled <= length; travelled += step) {
        const float t = travelled / length;
        dirty = dirty.united(stamp(target, {from.x + dx * t, from.y + dy * t}));
    }

    m_carry = travelled - length;
    m_last = to;
    return dirty;
}

RectI BrushEngine::stamp(PixelBuffer& target, Point2f center) const {
    const int size = std::max(1, m_brush.size);
    const float radius = static_cast<float>(size) * 0.5f;
    const bool odd = size % 2 == 1;
    const float cx = odd ? std::floor(center.x) + 0.5f : std::round(center.x);
    const float cy = odd ? std::floor(center.y) + 0.5f : std::round(center.y);

    // A slanted tip reaches past the plain size, so its stamp covers more
    // pixels than the size alone would suggest.
    const bool slanted = m_profile.angle != 0.0f;
    const float extent = slanted ? radius * std::hypot(1.0f, m_profile.nib_width) : radius;
    const int side = static_cast<int>(std::ceil(2.0f * extent)) + (slanted ? 2 : 1);
    const int left = static_cast<int>(std::floor(cx - extent));
    const int top = static_cast<int>(std::floor(cy - extent));
    const RectI area = RectI{left, top, side, side}.intersected(target.bounds());

    const BlendFn blend = blend_fn_for(BlendMode::Normal);
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            if (!m_selection.contains(x, y)) continue;
            const float ink = ink_at(m_profile, static_cast<float>(x) + 0.5f - cx,
                                     static_cast<float>(y) + 0.5f - cy, radius, x, y);
            if (ink <= 0.0f) continue;
            target.at(x, y) = ink >= 1.0f ? m_color : blend(target.at(x, y), m_color, ink);
        }
    }
    return area;
}

}  // namespace lsp
