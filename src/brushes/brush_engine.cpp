#include "brushes/brush_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

#include "config.hpp"
#include "core/blend_modes.hpp"
#include "core/blur.hpp"
#include "core/warp.hpp"
#include "utils/math.hpp"

namespace lsp {

namespace {

// How far a stroke must go before its direction is trusted.
constexpr float kOrientDistance = 3.0f;

std::uint32_t hash_of(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t hash = a * 374761393u + b * 668265263u + c * 2246822519u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return hash;
}

// A fixed value in 0..1 for each combination of inputs.
float unit_hash(int a, int b, std::uint32_t c) {
    return static_cast<float>(hash_of(static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), c) &
                              0xFFFFu) /
           65535.0f;
}

float smooth_unit(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Smoothly varying noise in 0..1 over the plane.
float value_noise(float x, float y, std::uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = smooth_unit(x - fx);
    const float ty = smooth_unit(y - fy);
    const float top = lerp(unit_hash(ix, iy, seed), unit_hash(ix + 1, iy, seed), tx);
    const float bottom = lerp(unit_hash(ix, iy + 1, seed), unit_hash(ix + 1, iy + 1, seed), tx);
    return lerp(top, bottom, ty);
}

// The paper's tooth at one pixel: 0 in the pits, 1 on the peaks.
float paper_tooth(int x, int y) {
    const float fx = static_cast<float>(x) + 0.5f;
    const float fy = static_cast<float>(y) + 0.5f;
    const float noise = 0.5f * value_noise(fx / 1.5f, fy / 1.5f, 11u) +
                        0.3f * value_noise(fx / 3.5f, fy / 3.5f, 23u) +
                        0.2f * value_noise(fx / 9.0f, fy / 9.0f, 37u);
    return std::clamp((noise - 0.5f) * 2.2f + 0.5f, 0.0f, 1.0f);
}

// How much paint one bristle carries at a point along the stroke: 1 is full.
float bristle_level(int bristle, float along_stroke, std::uint32_t seed) {
    const auto wave = [&](float scale, std::uint32_t salt) {
        const float position = along_stroke / scale;
        const float cell = std::floor(position);
        const int index = static_cast<int>(cell);
        return lerp(unit_hash(bristle, index, seed ^ salt), unit_hash(bristle, index + 1, seed ^ salt),
                    smooth_unit(position - cell));
    };
    return std::sqrt(0.65f * wave(14.0f, 0x51u) + 0.35f * wave(5.0f, 0xA7u));
}

// Where and how the tip sits for one stamp.
struct Tip {
    float radius = 0;
    float cos_a = 1;
    float sin_a = 0;
    float nib = 1;
    float path = 0;  // distance along the stroke at the tip's centre
    float bristle_width = 1;
    std::uint32_t seed = 0;
    std::uint32_t salt = 0;
};

struct Sample {
    float ink = 0;
    float shade = 0;
};

// How much ink the tip puts on one pixel, `dx` and `dy` from its centre.
Sample ink_at(const BrushProfile& profile, const Tip& tip, float dx, float dy, int x, int y) {
    if (tip.radius <= 0.0f) return {profile.alpha, 0.0f};
    // Distance from the middle, measured along and across the tip so that a
    // slanted chisel nib covers the pixels its angle says it should.
    const float along = dx * tip.cos_a + dy * tip.sin_a;
    const float across = -dx * tip.sin_a + dy * tip.cos_a;
    const float reach_radius = tip.radius + profile.feather;

    float limit = tip.nib;
    int bristle = 0;
    if (profile.follows_stroke) {
        bristle = floor_to_int(along / tip.bristle_width);
        limit *= 1.0f - profile.ragged * unit_hash(bristle, 0, tip.seed);
    }
    const float reach = profile.shape == BrushShape::Square
                            ? std::max(std::abs(along), std::abs(across) / limit) / reach_radius
                            : std::hypot(along, across / limit) / reach_radius;
    if (reach > 1.0f) return {};

    const float solid = std::clamp(profile.coverage, 0.0f, 1.0f);
    const float fade = reach <= solid ? 0.0f : (reach - solid) / (1.0f - solid);

    Sample sample;
    switch (profile.deposit) {
        case Deposit::Smooth: sample.ink = profile.alpha * (1.0f - fade); break;
        case Deposit::Spray: {
            const float chance = profile.density * (1.0f - reach) * (1.0f - reach);
            const bool lands = unit_hash(x, y, tip.salt) < chance;
            sample.ink = lands ? profile.alpha : 0.0f;
            break;
        }
        case Deposit::Waxy: {
            const float tooth = paper_tooth(x, y);
            // A ragged rim: the paper's peaks reach out further than its pits.
            const float rough = reach + (tooth - 0.5f) * 0.3f;
            if (rough > 1.0f) return {};
            const float pressure =
                profile.coverage * (1.0f - 0.45f * rough * rough) - profile.grain * tooth * 0.7f;
            sample.ink = profile.alpha * smooth_unit(pressure / 0.3f);
            break;
        }
        case Deposit::Graphite: {
            const float edge = 0.5f + 0.5f * std::cos(std::numbers::pi_v<float> * fade);
            const float tooth = paper_tooth(x, y);
            sample.ink = profile.alpha * edge * (1.0f - profile.grain * (1.0f - tooth));
            break;
        }
    }

    if (profile.follows_stroke && profile.bristle > 0.0f) {
        const float level = bristle_level(bristle, tip.path - across, tip.seed);
        sample.ink *= 1.0f - profile.bristle * (1.0f - level) * (1.0f - level);
        sample.shade = (level - 0.5f) * profile.tint;
    }
    if (profile.wet_edge > 0.0f) {
        const float rim = smooth_unit((std::abs(along) / reach_radius - 0.7f) / 0.3f);
        sample.ink = std::min(1.0f, sample.ink * (1.0f + profile.wet_edge * rim));
    }
    return sample;
}

Rgba8 shaded(Rgba8 color, float shade) {
    if (shade == 0.0f) return color;
    const auto shift = [&](std::uint8_t value) { return byte_from_unit(unit_from_byte(value) + shade); };
    return Rgba8{shift(color.r), shift(color.g), shift(color.b), color.a};
}

}  // namespace

BrushProfile BrushEngine::effective_profile(const Brush& brush) {
    BrushProfile profile = brush::profile(brush.kind);
    const float size = static_cast<float>(std::max(1, brush.size));
    switch (brush.kind) {
        case BrushKind::Brush:
            profile.shape = brush.shape;
            profile.spacing = brush.spacing;
            break;
        case BrushKind::Airbrush:
            // The same spray over a wider area lands fewer droplets per pixel.
            profile.density = std::clamp(0.8f * std::pow(10.0f / size, 0.9f), 0.004f, 0.9f);
            break;
        case BrushKind::NaturalPencil: {
            // A fine point is soft and light; only a broad tip lays down a solid core.
            const float breadth = std::clamp((size - 2.0f) / 12.0f, 0.0f, 1.0f);
            profile.alpha *= lerp(0.55f, 1.0f, breadth);
            profile.grain *= lerp(0.4f, 1.0f, breadth);
            profile.coverage *= breadth;
            profile.feather = lerp(0.9f, 0.3f, breadth);
            break;
        }
        default: break;
    }
    return profile;
}

void BrushEngine::prepare_source() {
    if (m_source_ready) return;
    m_source_ready = true;
    if (!m_source || m_brush.kind != BrushKind::Blur) return;
    const int columns = (m_source->width() + kBlurChunkSize - 1) / kBlurChunkSize;
    const int rows = (m_source->height() + kBlurChunkSize - 1) / kBlurChunkSize;
    m_blurred_columns = columns;
    m_blurred.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), PixelBuffer());
}

void BrushEngine::ensure_blurred(RectI area) {
    area = area.intersected(m_source->bounds());
    if (area.empty()) return;
    // A quarter of the size: the blur reaches as far as half the brush, which
    // keeps its effect inside the tip the user can see.
    const int radius = std::max(1, m_brush.size / 4);
    for (int row = area.y / kBlurChunkSize; row <= (area.bottom() - 1) / kBlurChunkSize; ++row) {
        for (int column = area.x / kBlurChunkSize; column <= (area.right() - 1) / kBlurChunkSize; ++column) {
            PixelBuffer& chunk = m_blurred[grid_index(column, row, m_blurred_columns)];
            if (chunk.width() > 0) continue;
            chunk = blur::box_twice_region(
                *m_source, radius,
                {column * kBlurChunkSize, row * kBlurChunkSize, kBlurChunkSize, kBlurChunkSize});
        }
    }
}

std::uint8_t& BrushEngine::coverage_at(int x, int y, int width, int height) {
    const int columns = (width + kCoverageChunkSize - 1) / kCoverageChunkSize;
    const int rows = (height + kCoverageChunkSize - 1) / kCoverageChunkSize;
    if (columns != m_coverage_columns || rows != m_coverage_rows) {
        m_coverage_columns = columns;
        m_coverage_rows = rows;
        m_coverage.assign(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), {});
    }
    std::vector<std::uint8_t>& chunk =
        m_coverage[grid_index(x / kCoverageChunkSize, y / kCoverageChunkSize, columns)];
    if (chunk.empty()) chunk.assign(static_cast<std::size_t>(kCoverageChunkSize) * kCoverageChunkSize, 0);
    return chunk[grid_index(x % kCoverageChunkSize, y % kCoverageChunkSize, kCoverageChunkSize)];
}

const Rgba8& BrushEngine::blurred_at(int x, int y) const {
    return m_blurred[grid_index(x / kBlurChunkSize, y / kBlurChunkSize, m_blurred_columns)].at(
        x % kBlurChunkSize, y % kBlurChunkSize);
}

void BrushEngine::steer(float dx, float dy) {
    const float length = std::hypot(dx, dy);
    if (length <= 0.0f) return;
    const float x = m_dir.x * 0.5f + dx / length * 0.5f;
    const float y = m_dir.y * 0.5f + dy / length * 0.5f;
    const float norm = std::hypot(x, y);
    if (norm > 1e-3f) m_dir = {x / norm, y / norm};
}

RectI BrushEngine::stroke_to(PixelBuffer& target, Point2f to) {
    prepare_source();
    const float step = std::max(1.0f, m_profile.spacing * static_cast<float>(m_brush.size));

    if (!m_last) {
        m_last = to;
        m_start = to;
        m_carry = step;
        m_seed = hash_of(static_cast<std::uint32_t>(round_to_int(to.x * 7.0f)),
                         static_cast<std::uint32_t>(round_to_int(to.y * 13.0f)), 0x9Eu);
        // A tip that faces the way the stroke goes waits until that is known.
        if (m_profile.follows_stroke) return {};
        // Nothing has moved yet, so a pushing tip has nothing to displace.
        return stamp(target, to, {0.0f, 0.0f}, 0.0f);
    }

    RectI dirty;
    if (m_profile.follows_stroke && !m_oriented) {
        if (distance(m_start, to) < kOrientDistance) return {};
        m_dir = {0.0f, 0.0f};
        steer(to.x - m_start.x, to.y - m_start.y);
        m_oriented = true;
        dirty = stamp(target, m_start, {0.0f, 0.0f}, 0.0f);
    }

    const Point2f from = *m_last;
    // How far the tip has already come: the pushing kind displaces by this much
    // rather than by the last step alone.
    const Point2f offset{from.x - m_start.x, from.y - m_start.y};
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    if (length <= 0.0f) return dirty;
    if (m_profile.follows_stroke && length >= 0.5f) steer(dx, dy);

    float travelled = m_carry;
    for (; travelled <= length; travelled += step) {
        const float t = travelled / length;
        const Point2f center{from.x + dx * t, from.y + dy * t};
        dirty = dirty.united(
            stamp(target, center, {offset.x + dx * t, offset.y + dy * t}, m_path + travelled));
    }

    m_carry = travelled - length;
    m_path += length;
    m_last = to;
    return dirty;
}

RectI BrushEngine::finish(PixelBuffer& target) {
    if (!m_last || !m_profile.follows_stroke || m_oriented) return {};
    m_oriented = true;
    m_dab = true;
    return stamp(target, m_start, {0.0f, 0.0f}, 0.0f);
}

void BrushEngine::pick_up(const PixelBuffer& canvas, float cx, float cy, float radius) {
    const float step = std::max(1.0f, m_profile.spacing * static_cast<float>(m_brush.size));
    const float rate = 1.0f - std::exp(-m_profile.pickup * step);
    const float reach = radius * 0.5f;
    const float taps[5][2] = {{0, 0}, {reach, 0}, {-reach, 0}, {0, reach}, {0, -reach}};
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float weight = 0.0f;
    for (const auto& tap : taps) {
        const int x = floor_to_int(cx + tap[0]);
        const int y = floor_to_int(cy + tap[1]);
        if (!canvas.contains(x, y)) continue;
        const Rgba8 pixel = canvas.at(x, y);
        // Blank paper carries no paint to pick up.
        const int ink = std::max({255 - pixel.r, 255 - pixel.g, 255 - pixel.b});
        const float alpha = unit_from_byte(pixel.a) * std::clamp(static_cast<float>(ink) / 64.0f, 0.0f, 1.0f);
        red += static_cast<float>(pixel.r) * alpha;
        green += static_cast<float>(pixel.g) * alpha;
        blue += static_cast<float>(pixel.b) * alpha;
        weight += alpha;
    }
    if (weight <= 0.0f) return;
    const float mix = rate * (weight / 5.0f);
    const auto blend_channel = [&](std::uint8_t current, float sum) {
        return static_cast<std::uint8_t>(std::lround(lerp(static_cast<float>(current), sum / weight, mix)));
    };
    m_loaded.r = blend_channel(m_loaded.r, red);
    m_loaded.g = blend_channel(m_loaded.g, green);
    m_loaded.b = blend_channel(m_loaded.b, blue);
}

RectI BrushEngine::stamp(PixelBuffer& target, Point2f center, Point2f move, float path) {
    const int size = std::max(1, m_brush.size);
    const float radius = static_cast<float>(size) * 0.5f;
    const bool odd = size % 2 == 1;
    const float cx = odd ? std::floor(center.x) + 0.5f : std::round(center.x);
    const float cy = odd ? std::floor(center.y) + 0.5f : std::round(center.y);

    Tip tip;
    tip.radius = radius;
    tip.path = path;
    tip.seed = m_seed;
    tip.salt = m_stamps++ ^ m_seed;
    tip.bristle_width = std::clamp(static_cast<float>(size) / 24.0f, 1.0f, 3.0f);
    const bool turning = m_profile.follows_stroke;
    if (turning) {
        // The tip's long axis lies across the direction of travel. A stroke
        // too short to have a direction leaves a square dab.
        tip.cos_a = -m_dir.y;
        tip.sin_a = m_dir.x;
        tip.nib = m_dab ? 1.0f : m_profile.nib_width;
    } else {
        const float radians = radians_from_degrees(m_profile.angle);
        tip.cos_a = std::cos(radians);
        tip.sin_a = std::sin(radians);
        tip.nib = m_profile.nib_width;
    }

    // A slanted or turning tip reaches past the plain size, so its stamp covers
    // more pixels than the size alone would suggest.
    const bool slanted = m_profile.angle != 0.0f || turning;
    const float reach_radius = radius + m_profile.feather;
    const float extent = slanted ? reach_radius * std::hypot(1.0f, tip.nib) : reach_radius;
    const int side = ceil_to_int(2.0f * extent) + 4;
    const int left = floor_to_int(cx - extent) - 1;
    const int top = floor_to_int(cy - extent) - 1;
    const RectI area = RectI{left, top, side, side}.intersected(target.bounds());

    const BlendFn blend = blend_fn_for(m_profile.blend);
    const bool blur_stamp = m_brush.kind == BrushKind::Blur && !m_blurred.empty() &&
                            m_source->width() >= target.width() &&
                            m_source->height() >= target.height();
    if (blur_stamp) ensure_blurred(area);
    const bool warp_stamp = m_brush.kind == BrushKind::Liquify && m_source != nullptr &&
                            m_source->width() >= target.width() &&
                            m_source->height() >= target.height();

    if (m_profile.pickup > 0.0f) {
        const bool sourced = m_source != nullptr && m_source->width() >= target.width() &&
                             m_source->height() >= target.height();
        pick_up(sourced ? *m_source : target, cx, cy, radius);
    }

    for (int y = area.y; y < area.bottom(); ++y) {
        const std::span<Rgba8> out = target.row(y);
        for (int x = area.x; x < area.right(); ++x) {
            if (!m_selection.contains(x, y)) continue;
            const Sample sample = ink_at(m_profile, tip, static_cast<float>(x) + 0.5f - cx,
                                         static_cast<float>(y) + 0.5f - cy, x, y);
            float ink = sample.ink;
            if (ink <= 0.0f) continue;
            Rgba8 color = shaded(m_loaded, sample.shade);
            if (warp_stamp) {
                // The pixel comes from where the tip was, less and less near the
                // rim, so the drag pulls the picture along with it.
                color = warp::sample_bilinear(*m_source, static_cast<float>(x) + 0.5f - move.x * ink,
                                              static_cast<float>(y) + 0.5f - move.y * ink);
            } else if (blur_stamp) {
                color = blurred_at(x, y);
            }
            if (m_profile.flat) {
                // Only the ink beyond what this stroke already put here is added.
                std::uint8_t& laid = coverage_at(x, y, target.width(), target.height());
                const std::uint8_t wanted = byte_from_unit(ink);
                if (wanted <= laid) continue;
                const float before = unit_from_byte(laid);
                laid = wanted;
                ink = (unit_from_byte(wanted) - before) / (1.0f - before);
            }
            Rgba8& pixel = out[static_cast<std::size_t>(x)];
            pixel = ink >= 1.0f ? color : blend(pixel, color, ink);
        }
    }
    return area;
}

}  // namespace lsp
