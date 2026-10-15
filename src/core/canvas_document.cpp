#include "core/canvas_document.hpp"

#include "core/compositor.hpp"

namespace lsp {

CanvasDocument::CanvasDocument(int width, int height) : m_flattened(width, height), m_selection(width, height) {
    m_layers.add({"Background", PixelBuffer(width, height, Rgba8{255, 255, 255, 255})});
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
    std::vector<PixelBuffer> result;
    for (const Layer& layer : m_layers.layers()) result.push_back(layer.pixels);
    return result;
}

void CanvasDocument::restore(std::vector<PixelBuffer> pixels) {
    for (std::size_t i = 0; i < pixels.size(); ++i) m_layers.at(i).pixels = std::move(pixels[i]);
    const PixelBuffer& base = m_layers.at(0).pixels;
    m_flattened = PixelBuffer(base.width(), base.height());
    // Restoring pixels says nothing about the selection: geometry commands
    // hand the mask back themselves, anything else starts deselected.
    m_selection = SelectionMask(base.width(), base.height());
    m_floating.reset();
    m_dirty = {};
    mark_dirty(bounds());
}

void CanvasDocument::resize(int width, int height, Rgba8 fill) {
    std::vector<PixelBuffer> resized;
    for (const Layer& layer : m_layers.layers()) {
        PixelBuffer pixels(width, height, resized.empty() ? fill : Rgba8{0, 0, 0, 0});
        pixels.blit(0, 0, layer.pixels);
        resized.push_back(std::move(pixels));
    }
    restore(std::move(resized));
}

void CanvasDocument::crop_to(RectI region) {
    region = region.intersected(bounds());
    if (region.empty()) return;
    std::vector<PixelBuffer> cropped;
    for (const Layer& layer : m_layers.layers()) cropped.push_back(layer.pixels.copy_region(region));
    restore(std::move(cropped));
}

void CanvasDocument::rotate(Rotation rotation) {
    std::vector<PixelBuffer> rotated;
    for (const Layer& layer : m_layers.layers()) {
        PixelBuffer result = layer.pixels.rotated_90_cw();
        if (rotation == Rotation::Ccw90) result = result.rotated_90_cw().rotated_90_cw();
        else if (rotation == Rotation::R180) result = result.rotated_90_cw();
        rotated.push_back(std::move(result));
    }
    restore(std::move(rotated));
}

void CanvasDocument::flip_horizontal() {
    std::vector<PixelBuffer> flipped;
    for (const Layer& layer : m_layers.layers()) flipped.push_back(layer.pixels.flipped_horizontal());
    restore(std::move(flipped));
}

void CanvasDocument::flip_vertical() {
    std::vector<PixelBuffer> flipped;
    for (const Layer& layer : m_layers.layers()) flipped.push_back(layer.pixels.flipped_vertical());
    restore(std::move(flipped));
}

}  // namespace lsp
