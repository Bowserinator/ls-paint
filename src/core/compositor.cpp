#include "core/compositor.hpp"

#include <algorithm>

namespace lsp {

void composite(const LayerStack& layers, RectI region, PixelBuffer& target) {
    region = region.intersected(target.bounds());
    const auto offset = static_cast<std::size_t>(region.x);
    const auto count = static_cast<std::size_t>(region.width);

    for (int y = region.y; y < region.bottom(); ++y) {
        const auto out = target.row(y).subspan(offset, count);
        std::ranges::fill(out, Rgba8{0, 0, 0, 0});

        for (const Layer& layer : layers.layers()) {
            if (!layer.visible) continue;
            // A layer of another size has nothing to add to this region.
            if (layer.pixels.width() != target.width() || layer.pixels.height() != target.height()) continue;
            const BlendFn blend = blend_fn_for(layer.blend_mode);
            const auto source = layer.pixels.row(y).subspan(offset, count);
            for (std::size_t i = 0; i < count; ++i)
                out[i] = blend(out[i], source[i], layer.opacity);
        }
    }
}

}  // namespace lsp
