#include "render/slint_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "config.hpp"
#include "render/tile_resample.hpp"
#include "utils/parallel.hpp"
#include "utils/math.hpp"
#include "utils/tiles.hpp"

namespace lsp {

static_assert(sizeof(Rgba8) == sizeof(slint::Rgba8Pixel));

namespace {

// Pixels of each tile image that lie past its edge, at the tile's own scale.
constexpr int kTileOverlap = 2;
// Tiles kept ready beyond the ones on screen, so a scroll does not wait on them,
// and how far out a tile may sit before its pixels are dropped.
constexpr int kPrefetchTiles = 1;
constexpr int kKeepTiles = 3;
// Past this many screen pixels per document pixel a tile in screen pixels grows
// too large to hold, so the tile image is left to be stretched by the window.
constexpr double kMaxDeviceScale = 4.0;

using TilePixels = slint::SharedPixelBuffer<slint::Rgba8Pixel>;

TilePixels pixels_of(const PixelBuffer& source, RectI area) {
    TilePixels pixels(static_cast<std::uint32_t>(area.width),
                                                     static_cast<std::uint32_t>(area.height));
    for (int y = 0; y < area.height; ++y) {
        const auto row = source.row(area.y + y).subspan(static_cast<std::size_t>(area.x),
                                                         static_cast<std::size_t>(area.width));
        std::memcpy(pixels.begin() + static_cast<std::size_t>(y) * pixels.width(), row.data(),
                    row.size_bytes());
    }
    return pixels;
}

TilePixels tile_pixels_from(const PixelGrid& grid) {
    TilePixels pixels(static_cast<std::uint32_t>(grid.width),
                                                     static_cast<std::uint32_t>(grid.height));
    if (!grid.pixels.empty()) std::memcpy(pixels.begin(), grid.pixels.data(), grid.pixels.size() * sizeof(Rgba8));
    return pixels;
}

// The tile's own pixels plus `overlap` more on the right and bottom, which lie
// under the neighbouring tiles so no gap can open between the two.
RectI tile_area(const PixelBuffer& source, int column, int row, int overlap) {
    return RectI{column * kDisplayTileSize, row * kDisplayTileSize, kDisplayTileSize + overlap,
                 kDisplayTileSize + overlap}
        .intersected(source.bounds());
}

}  // namespace

SlintBridge::SlintBridge() : m_tiles(std::make_shared<slint::VectorModel<slint::Image>>()) {}

double SlintBridge::device_scale() const { return static_cast<double>(m_zoom) * static_cast<double>(m_dpr); }

bool SlintBridge::set_zoom(float zoom, float dpr) {
    if (zoom == m_zoom && dpr == m_dpr) return false;
    m_zoom = zoom;
    m_dpr = dpr;
    const double scale = device_scale();
    const bool device = scale <= kMaxDeviceScale;
    const int level = quick_level(scale);
    if (!device && !m_device && !m_resampled && level == m_level) return false;
    m_device = device;
    m_resampled = false;
    m_level = level;
    // Forgetting the size makes the next upload rebuild every tile.
    m_width = 0;
    m_height = 0;
    return true;
}

bool SlintBridge::refine() {
    if (m_resampled || device_scale() >= 1.0) return false;
    m_resampled = true;
    m_width = 0;
    m_height = 0;
    return true;
}

float SlintBridge::tile_overlap() const {
    return m_resampled || m_device ? resample_overlap(device_scale()) : static_cast<float>(kTileOverlap << m_level);
}

bool SlintBridge::pixelated() const { return m_resampled || m_device || device_scale() >= 1.0; }

int SlintBridge::reach_before() const {
    if (!m_resampled && !m_device) return kTileOverlap << m_level;
    return static_cast<int>(std::ceil(tile_overlap())) + reach_after();
}

int SlintBridge::reach_after() const {
    const double scale = device_scale();
    if (m_resampled) return static_cast<int>(std::ceil(1.0 / scale)) + 2 * (1 << resample_level(scale));
    if (m_device) return static_cast<int>(std::ceil(1.0 / scale)) + (1 << m_level);
    return 0;
}

SlintBridge::TileRange SlintBridge::range_around(RectI view, int margin) const {
    if (!m_has_view) return {0, m_columns - 1, 0, m_rows - 1};
    const auto tile_of = [](int pixel) { return pixel >= 0 ? pixel / kDisplayTileSize : -1; };
    const int right = std::max(view.x, view.right() - 1);
    const int bottom = std::max(view.y, view.bottom() - 1);
    return {std::clamp(tile_of(view.x) - margin, 0, m_columns - 1),
            std::clamp(tile_of(right) + margin, 0, m_columns - 1),
            std::clamp(tile_of(view.y) - margin, 0, m_rows - 1),
            std::clamp(tile_of(bottom) + margin, 0, m_rows - 1)};
}

void SlintBridge::build(const PixelBuffer& source, const std::vector<std::size_t>& indices) {
    if (indices.empty()) return;
    std::vector<TilePixels> built(indices.size());
    // Only the pixels are made on the other threads. The images and the model
    // belong to the thread that runs the window.
    if (m_resampled || m_device) {
        const std::vector<PixelGrid> grids = screen_grid_tiles(source, indices);
        parallel_for(indices.size(), [&](std::size_t k) { built[k] = tile_pixels_from(grids[k]); });
    } else {
        const int overlap = kTileOverlap << m_level;
        parallel_for(indices.size(), [&](std::size_t k) {
            const int column = static_cast<int>(indices[k]) % m_columns;
            const int row = static_cast<int>(indices[k]) / m_columns;
            const RectI area = tile_area(source, column, row, overlap);
            built[k] = m_level == 0 ? pixels_of(source, area)
                                    : tile_pixels_from(box_reduced(source, area, 1 << m_level));
        });
    }
    for (std::size_t k = 0; k < indices.size(); ++k) {
        m_tiles->set_row_data(indices[k], slint::Image(built[k]));
        m_present[indices[k]] = true;
    }
}

std::vector<PixelGrid> SlintBridge::screen_grid_tiles(const PixelBuffer& source,
                                                      const std::vector<std::size_t>& indices) const {
    std::vector<PixelGrid> grids(indices.size());
    const float overlap = tile_overlap();
    const double scale = device_scale();
    const auto box_of = [&](std::size_t index) {
        return tile_device_box(static_cast<int>(index) % m_columns, static_cast<int>(index) / m_columns,
                               kDisplayTileSize, overlap, m_zoom, m_dpr, source.width(), source.height());
    };
    // Tiles side by side in a row are filtered as one picture and then cut
    // apart, so nothing about a tile's neighbours can differ from what the
    // neighbours themselves show.
    for (std::size_t first = 0; first < indices.size();) {
        std::size_t last = first + 1;
        while (last < indices.size() && indices[last] == indices[last - 1] + 1 &&
               static_cast<int>(indices[last]) % m_columns != 0)
            ++last;
        DeviceBox span = box_of(indices[first]);
        for (std::size_t k = first + 1; k < last; ++k) span = united(span, box_of(indices[k]));
        const PixelGrid picture = m_resampled ? resampled(source, span, scale) : sampled(source, span, scale, m_level);
        for (std::size_t k = first; k < last; ++k)
            grids[k] = cropped(picture, span, box_of(indices[k]));
        first = last;
    }
    return grids;
}

void SlintBridge::upload(const PixelBuffer& source, RectI region) {
    if (source.width() != m_width || source.height() != m_height) {
        m_width = source.width();
        m_height = source.height();
        m_columns = ceil_div(m_width, kDisplayTileSize);
        m_rows = ceil_div(m_height, kDisplayTileSize);
        const auto count = static_cast<std::size_t>(m_columns) * static_cast<std::size_t>(m_rows);
        m_present.assign(count, false);
        m_tiles->set_vector(std::vector<slint::Image>(count));
        show_range(source);
        return;
    }

    region = region.intersected(source.bounds());
    if (region.empty()) return;
    // A tile also shows the pixels just past its edge, so the tiles to the left
    // and above change with them.
    const int before = reach_before();
    const int after = reach_after();
    region = RectI{region.x - before, region.y - before, region.width + before + after,
                   region.height + before + after}
                 .intersected(source.bounds());
    // Tiles that are not showing are left alone; they are built from the new
    // pixels when they come into view.
    std::vector<std::size_t> stale;
    for_each_tile(region, kDisplayTileSize, [&](int column, int row) {
        const std::size_t index = grid_index(column, row, m_columns);
        if (m_present[index]) stale.push_back(index);
    });
    build(source, stale);
}

void SlintBridge::set_view(const PixelBuffer& source, RectI view) {
    m_view = view;
    m_has_view = true;
    if (m_width == 0 || source.width() != m_width || source.height() != m_height) return;
    show_range(source);
}

void SlintBridge::show_range(const PixelBuffer& source) {
    const TileRange wanted = range_around(m_view, kPrefetchTiles);
    const TileRange kept = range_around(m_view, kKeepTiles);
    std::vector<std::size_t> missing;
    for (int row = 0; row < m_rows; ++row)
        for (int column = 0; column < m_columns; ++column) {
            const std::size_t index = grid_index(column, row, m_columns);
            const bool wanted_here = column >= wanted.first_column && column <= wanted.last_column &&
                                     row >= wanted.first_row && row <= wanted.last_row;
            const bool kept_here = column >= kept.first_column && column <= kept.last_column &&
                                   row >= kept.first_row && row <= kept.last_row;
            if (wanted_here && !m_present[index]) missing.push_back(index);
            if (!kept_here && m_present[index]) {
                m_tiles->set_row_data(index, slint::Image());
                m_present[index] = false;
            }
        }
    build(source, missing);
}

slint::Image image_from(const PixelBuffer& source) { return slint::Image(pixels_of(source, source.bounds())); }

}  // namespace lsp
