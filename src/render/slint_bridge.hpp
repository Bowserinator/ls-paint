#pragma once

#include <memory>

#include <slint.h>

#include "core/pixel_buffer.hpp"

namespace lsp {

// Holds the canvas as a grid of images, so changing a few pixels only touches
// the few images they fall in.
class SlintBridge {
public:
    SlintBridge(int width, int height);

    void upload(const PixelBuffer& source, RectI region);

    // The images in row-major order, which the window lays out as a grid.
    [[nodiscard]] std::shared_ptr<slint::Model<slint::Image>> tiles() const { return m_tiles; }

private:
    int m_width = 0;
    int m_height = 0;
    int m_columns = 0;
    std::shared_ptr<slint::VectorModel<slint::Image>> m_tiles;
};

// Copies `source` into an image that stands on its own.
[[nodiscard]] slint::Image image_from(const PixelBuffer& source);

}  // namespace lsp
