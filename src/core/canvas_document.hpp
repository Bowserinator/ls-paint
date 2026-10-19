#pragma once

#include <optional>
#include <vector>

#include "core/layer_stack.hpp"
#include "core/pixel_buffer.hpp"
#include "selection/selection_mask.hpp"
#include "selection/selection_ops.hpp"

namespace lsp {

class CanvasDocument {
public:
    CanvasDocument(int width, int height);

    [[nodiscard]] int width() const noexcept { return base().width(); }
    [[nodiscard]] int height() const noexcept { return base().height(); }
    [[nodiscard]] RectI bounds() const noexcept { return base().bounds(); }

    [[nodiscard]] LayerStack& layers() noexcept { return m_layers; }
    [[nodiscard]] SelectionMask& selection() noexcept { return m_selection; }
    [[nodiscard]] const SelectionMask& selection() const noexcept { return m_selection; }
    // Selection contents that have not been written to the layer yet. Empty
    // between commits.
    [[nodiscard]] std::optional<FloatingContents>& floating() noexcept { return m_floating; }
    [[nodiscard]] const std::optional<FloatingContents>& floating() const noexcept { return m_floating; }
    [[nodiscard]] Layer& active_layer() { return m_layers.active(); }
    // The image as it is shown: the layer, with any floating block drawn over it
    // once flush() has placed the block.
    [[nodiscard]] const PixelBuffer& flattened() const noexcept;

    void mark_dirty(RectI region);
    std::optional<RectI> flush();

    // A color the floating contents leave out when they are drawn, so what is
    // under them shows through wherever they have it.
    [[nodiscard]] const std::optional<Rgba8>& transparent_color() const noexcept { return m_transparent_color; }
    void set_transparent_color(std::optional<Rgba8> color);

    [[nodiscard]] std::vector<PixelBuffer> snapshot() const;
    void restore(std::vector<PixelBuffer> pixels);
    // Exchanges the layer pixels with `pixels` without copying them.
    void swap_pixels(std::vector<PixelBuffer>& pixels);
    void resize(int width, int height, Rgba8 fill);
    void crop_to(RectI region);
    // Stretches every layer to the new size.
    void scale(int width, int height);
    // Shears every layer. The room the lean needs is added, and the bottom layer
    // shows `fill` where the lean leaves it uncovered.
    void skew(float horizontal_degrees, float vertical_degrees, Rgba8 fill);

    enum class Rotation { Cw90, Ccw90, R180 };
    void rotate(Rotation rotation);
    void flip_horizontal();
    void flip_vertical();

private:
    [[nodiscard]] const PixelBuffer& base() const noexcept { return m_layers.layers().front().pixels; }
    // Forgets the picture of the floating block and marks everything dirty.
    void rebuild_flattened();

    LayerStack m_layers;
    // The layer with the floating block drawn over it. Empty unless a block is
    // floating, because the layer is the image the rest of the time.
    PixelBuffer m_with_floating;
    RectI m_dirty;
    SelectionMask m_selection;
    std::optional<FloatingContents> m_floating;
    std::optional<Rgba8> m_transparent_color;
};

}  // namespace lsp
