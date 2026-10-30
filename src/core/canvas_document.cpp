#include "core/canvas_document.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace lsp {

namespace {

std::atomic<std::uint64_t> next_generation{1};

// One buffer per layer, each made from the layer's own pixels.
template <typename MakeFn>
std::vector<PixelBuffer> map_layers(const LayerStack& layers, MakeFn make) {
    std::vector<PixelBuffer> result;
    result.reserve(layers.layers().size());
    for (const Layer& layer : layers.layers()) result.push_back(make(layer.pixels));
    return result;
}

}  // namespace

CanvasDocument::CanvasDocument(int width, int height)
    : m_selection(width, height), m_generation(next_generation.fetch_add(1)) {
    m_layers.add({"Background", PixelBuffer(width, height, kWhite)});
    mark_dirty(bounds());
}

void CanvasDocument::mark_dirty(RectI region) {
    m_dirty = m_dirty.united(region.intersected(bounds()));
}

void CanvasDocument::set_transparent_color(std::optional<Rgba8> color) {
    if (color == m_transparent_color) return;
    m_transparent_color = color;
    if (m_floating) mark_dirty(bounds());
}

const PixelBuffer& CanvasDocument::flattened() const noexcept {
    // A block that has not been flushed yet is not in the picture either.
    return m_floating && m_with_floating.width() > 0 ? m_with_floating : base();
}

std::optional<RectI> CanvasDocument::flush() {
    if (m_dirty.empty()) return std::nullopt;
    const RectI region = m_dirty;
    m_dirty = {};
    if (!m_floating) {
        m_with_floating = {};
        return region;
    }
    // Starting from the layer's own bands, only the bands the block reaches are
    // ever copied. Whatever the block covered before is put back from the layer
    // first, so a block that moved leaves nothing behind.
    if (m_with_floating.width() == 0) m_with_floating = base();
    m_with_floating.copy_region_from(base(), region);
    draw_floating(m_with_floating, *m_floating, region, m_transparent_color);
    return region;
}

std::vector<PixelBuffer> CanvasDocument::snapshot() const {
    return map_layers(m_layers, [](const PixelBuffer& pixels) { return pixels; });
}

void CanvasDocument::rebuild_flattened() {
    m_generation = next_generation.fetch_add(1);
    m_with_floating = {};
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

void CanvasDocument::scale(int width, int height) {
    restore(map_layers(m_layers, [width, height](const PixelBuffer& pixels) { return pixels.scaled(width, height); }));
}

void CanvasDocument::skew(float horizontal_degrees, float vertical_degrees, Rgba8 fill) {
    std::vector<PixelBuffer> skewed;
    for (const Layer& layer : m_layers.layers()) {
        const Rgba8 uncovered = skewed.empty() ? fill : kTransparent;
        skewed.push_back(layer.pixels.skewed_horizontal(horizontal_degrees, uncovered)
                             .skewed_vertical(vertical_degrees, uncovered));
    }
    restore(std::move(skewed));
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
