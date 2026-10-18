#include "render/slint_bridge.hpp"

#include <cstring>
#include <vector>

#include "config.hpp"
#include "utils/math.hpp"

namespace lsp {

static_assert(sizeof(Rgba8) == sizeof(slint::Rgba8Pixel));

namespace {

slint::Image image_of(const PixelBuffer& source, RectI area) {
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(static_cast<std::uint32_t>(area.width),
                                                     static_cast<std::uint32_t>(area.height));
    for (int y = 0; y < area.height; ++y) {
        const auto row = source.row(area.y + y).subspan(static_cast<std::size_t>(area.x),
                                                         static_cast<std::size_t>(area.width));
        std::memcpy(pixels.begin() + static_cast<std::size_t>(y) * pixels.width(), row.data(),
                    row.size_bytes());
    }
    return slint::Image(pixels);
}

RectI tile_area(const PixelBuffer& source, int column, int row) {
    return RectI{column * kDisplayTileSize, row * kDisplayTileSize, kDisplayTileSize, kDisplayTileSize}
        .intersected(source.bounds());
}

}  // namespace

SlintBridge::SlintBridge(int, int) : m_tiles(std::make_shared<slint::VectorModel<slint::Image>>()) {}

void SlintBridge::upload(const PixelBuffer& source, RectI region) {
    const int columns = (source.width() + kDisplayTileSize - 1) / kDisplayTileSize;
    const int rows = (source.height() + kDisplayTileSize - 1) / kDisplayTileSize;

    if (source.width() != m_width || source.height() != m_height) {
        m_width = source.width();
        m_height = source.height();
        m_columns = columns;
        std::vector<slint::Image> tiles;
        tiles.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                tiles.push_back(image_of(source, tile_area(source, column, row)));
        m_tiles->set_vector(std::move(tiles));
        return;
    }

    region = region.intersected(source.bounds());
    if (region.empty()) return;
    for (int row = region.y / kDisplayTileSize; row <= (region.bottom() - 1) / kDisplayTileSize; ++row)
        for (int column = region.x / kDisplayTileSize; column <= (region.right() - 1) / kDisplayTileSize;
             ++column)
            m_tiles->set_row_data(grid_index(column, row, m_columns),
                                  image_of(source, tile_area(source, column, row)));
}

slint::Image image_from(const PixelBuffer& source) {
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(static_cast<std::uint32_t>(source.width()),
                                                     static_cast<std::uint32_t>(source.height()));

    for (int y = 0; y < source.height(); ++y) {
        const auto row = source.row(y);
        std::memcpy(pixels.begin() + static_cast<std::size_t>(y) * pixels.width(), row.data(),
                    row.size_bytes());
    }
    return slint::Image(pixels);
}

}  // namespace lsp
