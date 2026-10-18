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

    [[nodiscard]] int width() const noexcept { return m_flattened.width(); }
    [[nodiscard]] int height() const noexcept { return m_flattened.height(); }
    [[nodiscard]] RectI bounds() const noexcept { return m_flattened.bounds(); }

    [[nodiscard]] LayerStack& layers() noexcept { return m_layers; }
    [[nodiscard]] SelectionMask& selection() noexcept { return m_selection; }
    [[nodiscard]] const SelectionMask& selection() const noexcept { return m_selection; }
    // Selection contents that have not been written to the layer yet. Empty
    // between commits.
    [[nodiscard]] std::optional<FloatingContents>& floating() noexcept { return m_floating; }
    [[nodiscard]] const std::optional<FloatingContents>& floating() const noexcept { return m_floating; }
    [[nodiscard]] Layer& active_layer() { return m_layers.active(); }
    [[nodiscard]] const PixelBuffer& flattened() const noexcept { return m_flattened; }

    void mark_dirty(RectI region);
    std::optional<RectI> flush();

    [[nodiscard]] std::vector<PixelBuffer> snapshot() const;
    void restore(std::vector<PixelBuffer> pixels);
    // Exchanges the layer pixels with `pixels` without copying them.
    void swap_pixels(std::vector<PixelBuffer>& pixels);
    void resize(int width, int height, Rgba8 fill);
    void crop_to(RectI region);

    enum class Rotation { Cw90, Ccw90, R180 };
    void rotate(Rotation rotation);
    void flip_horizontal();
    void flip_vertical();

private:
    // Replaces the flattened image with an empty one the size of the layers and
    // marks all of it dirty.
    void rebuild_flattened();

    LayerStack m_layers;
    PixelBuffer m_flattened;
    RectI m_dirty;
    SelectionMask m_selection;
    std::optional<FloatingContents> m_floating;
};

}  // namespace lsp
