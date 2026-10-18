#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <slint.h>

#include "core/pixel_buffer.hpp"

namespace lsp {

// Holds the canvas as a grid of images, so changing a few pixels only touches
// the few images they fall in. Only the images near what is on screen are
// kept, which lets canvases far larger than memory could hold as images be
// shown.
class SlintBridge {
public:
    SlintBridge(int width, int height);

    void upload(const PixelBuffer& source, RectI region);

    // Shows the canvas averaged down by 2^level, for views zoomed out that far.
    // Returns whether the level changed, in which case every tile is stale.
    bool set_level(int level);

    // Tells which part of the canvas is on screen, in document pixels, so the
    // images around it are made and the ones far from it are dropped.
    void set_view(const PixelBuffer& source, RectI view);

    // The images in row-major order, which the window lays out as a grid.
    [[nodiscard]] std::shared_ptr<slint::Model<slint::Image>> tiles() const { return m_tiles; }

private:
    struct TileRange {
        int first_column = 0;
        int last_column = 0;
        int first_row = 0;
        int last_row = 0;
    };

    // The tiles within `margin` tiles of the view.
    [[nodiscard]] TileRange range_around(RectI view, int margin) const;
    // Makes the images of the tiles at `indices`, spreading the work over the
    // machine's cores.
    void build(const PixelBuffer& source, const std::vector<std::size_t>& indices);
    // Builds the tiles near the view that are missing and drops the far ones.
    void show_range(const PixelBuffer& source);

    int m_width = 0;
    int m_height = 0;
    int m_columns = 0;
    int m_rows = 0;
    int m_level = 0;
    RectI m_view;
    bool m_has_view = false;
    std::vector<bool> m_present;  // whether each tile holds its image
    std::shared_ptr<slint::VectorModel<slint::Image>> m_tiles;
};

// Copies `source` into an image that stands on its own.
[[nodiscard]] slint::Image image_from(const PixelBuffer& source);

}  // namespace lsp
