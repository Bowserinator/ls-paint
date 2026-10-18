#include "core/blend_modes.hpp"

#include <algorithm>

#include "utils/math.hpp"

namespace lsp {

namespace {

template <typename MixFn>
Rgba8 blend_with(Rgba8 base, Rgba8 top, float opacity, MixFn mix) {
    const float top_alpha = unit_from_byte(top.a) * opacity;
    const float base_alpha = unit_from_byte(base.a);
    const float out_alpha = top_alpha + base_alpha * (1.0f - top_alpha);
    if (out_alpha <= 0.0f) return kTransparent;

    const auto channel = [&](std::uint8_t base_value, std::uint8_t top_value) {
        const float cb = unit_from_byte(base_value);
        const float cs = unit_from_byte(top_value);
        const float result = ((1.0f - top_alpha) * base_alpha * cb +
                              top_alpha * ((1.0f - base_alpha) * cs + base_alpha * mix(cb, cs))) /
                             out_alpha;
        return byte_from_unit(result);
    };

    return Rgba8{channel(base.r, top.r), channel(base.g, top.g), channel(base.b, top.b),
                 byte_from_unit(out_alpha)};
}

Rgba8 blend_normal(Rgba8 base, Rgba8 top, float opacity) {
    return blend_with(base, top, opacity, [](float, float cs) { return cs; });
}

Rgba8 blend_multiply(Rgba8 base, Rgba8 top, float opacity) {
    return blend_with(base, top, opacity, [](float cb, float cs) { return cb * cs; });
}

Rgba8 blend_screen(Rgba8 base, Rgba8 top, float opacity) {
    return blend_with(base, top, opacity, [](float cb, float cs) { return cb + cs - cb * cs; });
}

}  // namespace

BlendFn blend_fn_for(BlendMode mode) {
    switch (mode) {
        case BlendMode::Multiply: return &blend_multiply;
        case BlendMode::Screen: return &blend_screen;
        case BlendMode::Normal: break;
    }
    return &blend_normal;
}

}  // namespace lsp
