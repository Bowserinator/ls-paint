#include "core/blend_modes.hpp"

#include <algorithm>

namespace lsp {

namespace {

template <typename MixFn>
Rgba8 blend_with(Rgba8 base, Rgba8 top, float opacity, MixFn mix) {
    const float top_alpha = static_cast<float>(top.a) / 255.0f * opacity;
    const float base_alpha = static_cast<float>(base.a) / 255.0f;
    const float out_alpha = top_alpha + base_alpha * (1.0f - top_alpha);
    if (out_alpha <= 0.0f) return Rgba8{0, 0, 0, 0};

    const auto channel = [&](std::uint8_t base_value, std::uint8_t top_value) {
        const float cb = static_cast<float>(base_value) / 255.0f;
        const float cs = static_cast<float>(top_value) / 255.0f;
        const float result = ((1.0f - top_alpha) * base_alpha * cb +
                              top_alpha * ((1.0f - base_alpha) * cs + base_alpha * mix(cb, cs))) /
                             out_alpha;
        return static_cast<std::uint8_t>(std::clamp(result, 0.0f, 1.0f) * 255.0f + 0.5f);
    };

    return Rgba8{channel(base.r, top.r), channel(base.g, top.g), channel(base.b, top.b),
                 static_cast<std::uint8_t>(out_alpha * 255.0f + 0.5f)};
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
