#include "core/canvas_document.hpp"

#include <algorithm>
#include <utility>

#include "core/compositor.hpp"

namespace lsp {

namespace {

// One buffer per layer, each made from the layer's own pixels.
template <typename MakeFn>
std::vector<PixelBuffer> map_layers(const LayerStack& layers, MakeFn make) {
    std::vector<PixelBuffer> result;
    result.reserve(layers.layers().size());
    for (const Layer& layer : layers.layers()) result.push_back(make(layer.pixels));
    return result;
}

}  // namespace

CanvasDocument::CanvasDocument(int width, int height) : m_flattened(width, height), m_selection(width, height) {
    m_layers.add({"Background", PixelBuffer(width, height, kWhite)});
    mark_dirty(bounds());
}

void CanvasDocument::mark_dirty(RectI region) {
    m_dirty = m_dirty.united(region.intersected(bounds()));
}

std::optional<RectI> CanvasDocument::flush() {
    if (m_dirty.empty()) return std::nullopt;
    const RectI region = m_dirty;
    m_dirty = {};
    composite(m_layers, region, m_flattened);
    // Whatever is floating is painted over the freshly composited pixels, so a
    // layer edit under the float still ends up showing the float on top.
    if (m_floating) draw_floating(m_flattened, *m_floating, region);
    return region;
}

std::vector<PixelBuffer> CanvasDocument::snapshot() const {
    return map_layers(m_layers, [](const PixelBuffer& pixels) { return pixels; });
}

void CanvasDocument::rebuild_flattened() {
    const PixelBuffer& base = m_layers.at(0).pixels;
    m_flattened = PixelBuffer(base.width(), base.height());
    m_dirty = {};
    mark_dirty(bounds());
}

void CanvasDocument::restore(std::vector<PixelBuffer> pixels) {
    // A snapshot holds one buffer per layer; anything else would leave part of
    // the stack at the old size.
    const std::size_t count = std::min(pixels.size(), m_layers.layers().size());
    if (count == 0) return;
    for (std::size_t i = 0; i < count; ++i) m_layers.at(i).pixels = std::move(pixels[i]);
    rebuild_flattened();
    // Restoring pixels says nothing about the selection: geometry commands
    // hand the mask back themselves, anything else starts deselected.
    m_selection = SelectionMask(width(), height());
    m_floating.reset();
}

void CanvasDocument::swap_pixels(std::vector<PixelBuffer>& pixels) {
    // One buffer per layer, as in restore(). The selection and the floating
    // block are the caller's business.
    const std::size_t count = std::min(pixels.size(), m_layers.layers().size());
    if (count == 0) return;
    for (std::size_t i = 0; i < count; ++i) std::swap(m_layers.at(i).pixels, pixels[i]);
    rebuild_flattened();
}

void CanvasDocument::resize(int width, int height, Rgba8 fill) {
    std::vector<PixelBuffer> resized;
    for (const Layer& layer : m_layers.layers()) {
        PixelBuffer pixels(width, height, resized.empty() ? fill : kTransparent);
        pixels.blit(0, 0, layer.pixels);
        resized.push_back(std::move(pixels));
    }
    restore(std::move(resized));
}

void CanvasDocument::crop_to(RectI region) {
    region = region.intersected(bounds());
    if (region.empty()) return;
    restore(map_layers(m_layers, [region](const PixelBuffer& pixels) { return pixels.copy_region(region); }));
}

void CanvasDocument::rotate(Rotation rotation) {
    restore(map_layers(m_layers, [rotation](const PixelBuffer& pixels) {
        switch (rotation) {
            case Rotation::Ccw90: return pixels.rotated_90_ccw();
            case Rotation::R180: return pixels.rotated_180();
            case Rotation::Cw90: break;
        }
        return pixels.rotated_90_cw();
    }));
}

void CanvasDocument::flip_horizontal() {
    restore(map_layers(m_layers, [](const PixelBuffer& pixels) { return pixels.flipped_horizontal(); }));
}

void CanvasDocument::flip_vertical() {
    restore(map_layers(m_layers, [](const PixelBuffer& pixels) { return pixels.flipped_vertical(); }));
}

}  // namespace lsp
