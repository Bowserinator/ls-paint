#include "core/warp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lsp::warp {

Rgba8 sample_bilinear(const PixelBuffer& source, float x, float y) noexcept {
    if (source.width() <= 0 || source.height() <= 0) return Rgba8{0, 0, 0, 0};

    const float fx = std::clamp(x - 0.5f, 0.0f, static_cast<float>(source.width() - 1));
    const float fy = std::clamp(y - 0.5f, 0.0f, static_cast<float>(source.height() - 1));
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, source.width() - 1);
    const int y1 = std::min(y0 + 1, source.height() - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const Rgba8 taps[4] = {source.at(x0, y0), source.at(x1, y0), source.at(x0, y1),
                           source.at(x1, y1)};
    const float weights[4] = {(1.0f - tx) * (1.0f - ty), tx * (1.0f - ty),
                              (1.0f - tx) * ty, tx * ty};

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float weight = weights[i];
        const float tap_alpha = static_cast<float>(taps[i].a) / 255.0f;
        red += weight * static_cast<float>(taps[i].r) * tap_alpha;
        green += weight * static_cast<float>(taps[i].g) * tap_alpha;
        blue += weight * static_cast<float>(taps[i].b) * tap_alpha;
        alpha += weight * tap_alpha;
    }
    if (alpha <= 0.0f) return Rgba8{0, 0, 0, 0};

    const auto channel = [alpha](float value) {
        return static_cast<std::uint8_t>(std::clamp(value / alpha + 0.5f, 0.0f, 255.0f));
    };
    return Rgba8{channel(red), channel(green), channel(blue),
                 static_cast<std::uint8_t>(std::clamp(alpha * 255.0f + 0.5f, 0.0f, 255.0f))};
}

}  // namespace lsp::warp
