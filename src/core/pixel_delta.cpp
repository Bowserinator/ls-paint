#include "core/pixel_delta.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_set>

#include "config.hpp"

namespace lsp {

namespace {

bool differs(const PixelBuffer& before, const PixelBuffer& after, RectI area) {
    for (int y = area.y; y < area.bottom(); ++y) {
        if (before.row_shared_with(after, y)) continue;
        const auto x = static_cast<std::size_t>(area.x);
        const auto count = static_cast<std::size_t>(area.width);
        if (!std::ranges::equal(before.row(y).subspan(x, count), after.row(y).subspan(x, count))) return true;
    }
    return false;
}

}  // namespace

PixelDelta PixelDelta::between(const PixelBuffer& before, const PixelBuffer& after, RectI region) {
    PixelDelta delta;
    if (before.width() != after.width() || before.height() != after.height()) return delta;
    region = region.intersected(before.bounds());
    if (region.empty()) return delta;

    // The squares never straddle two bands, so a band decides on its own
    // whether to keep its squares or itself.
    for (int index = 0; index < before.band_count(); ++index) {
        const RectI rows = before.band_rows(index);
        if (rows.bottom() <= region.y || rows.y >= region.bottom()) continue;
        if (before.band(index) == after.band(index)) continue;

        const int top = std::max(rows.y, region.y);
        const int bottom = std::min(rows.bottom(), region.bottom());
        std::vector<RectI> changed;
        long long changed_pixels = 0;
        for (int y = top; y < bottom; y += kUndoTileSize) {
            for (int x = region.x; x < region.right(); x += kUndoTileSize) {
                const RectI area{x, y, std::min(kUndoTileSize, region.right() - x),
                                 std::min(kUndoTileSize, bottom - y)};
                if (!differs(before, after, area)) continue;
                changed.push_back(area);
                changed_pixels += static_cast<long long>(area.width) * area.height;
            }
        }
        if (changed.empty()) continue;

        const bool covers_band = region.x <= 0 && region.right() >= before.width() && top == rows.y &&
                                 bottom == rows.bottom();
        const long long band_pixels = static_cast<long long>(rows.width) * rows.height;
        if (covers_band && changed_pixels * 2 >= band_pixels) {
            delta.m_bands.push_back({index, before.band(index), after.band(index)});
            delta.m_bounds = delta.m_bounds.united(rows);
            continue;
        }
        for (const RectI& area : changed) {
            Tile tile{area, before.copy_region(area), after.copy_region(area)};
            delta.m_bytes += pixel_bytes(tile.before) + pixel_bytes(tile.after);
            delta.m_tiles.push_back(std::move(tile));
            delta.m_bounds = delta.m_bounds.united(area);
        }
    }
    delta.m_bytes += delta.band_bytes();
    return delta;
}

std::size_t PixelDelta::band_bytes() const {
    // A band both versions hold, or one that many bands of a fresh canvas hold,
    // is storage once.
    std::unordered_set<const void*> seen;
    std::size_t total = 0;
    for (const Band& band : m_bands) {
        for (const PixelBuffer::Band& storage : {band.before, band.after})
            if (seen.insert(storage.get()).second) total += storage->size() * sizeof(Rgba8);
    }
    return total;
}

void PixelDelta::append(PixelDelta&& other) {
    m_bounds = m_bounds.united(other.m_bounds);
    m_bytes += other.m_bytes;
    m_tiles.insert(m_tiles.end(), std::make_move_iterator(other.m_tiles.begin()),
                   std::make_move_iterator(other.m_tiles.end()));
    m_bands.insert(m_bands.end(), std::make_move_iterator(other.m_bands.begin()),
                   std::make_move_iterator(other.m_bands.end()));
}

void PixelDelta::apply(PixelBuffer& target, const Tile& tile, const PixelBuffer& pixels) {
    target.blit(tile.area.x, tile.area.y, pixels);
}

bool PixelDelta::fits(const PixelBuffer& target, const Band& band) const noexcept {
    if (band.index >= target.band_count()) return false;
    const RectI rows = target.band_rows(band.index);
    return band.before->size() == static_cast<std::size_t>(rows.width) * static_cast<std::size_t>(rows.height);
}

void PixelDelta::undo(PixelBuffer& target) const {
    for (const Band& band : m_bands)
        if (fits(target, band)) target.set_band(band.index, band.before);
    for (const Tile& tile : m_tiles) apply(target, tile, tile.before);
}

void PixelDelta::redo(PixelBuffer& target) const {
    for (const Band& band : m_bands)
        if (fits(target, band)) target.set_band(band.index, band.after);
    for (const Tile& tile : m_tiles) apply(target, tile, tile.after);
}

}  // namespace lsp
