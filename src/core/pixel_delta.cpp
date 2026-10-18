#include "core/pixel_delta.hpp"

#include <algorithm>
#include <iterator>

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

    for (int y = region.y; y < region.bottom(); y += kUndoTileSize) {
        for (int x = region.x; x < region.right(); x += kUndoTileSize) {
            const RectI area{x, y, std::min(kUndoTileSize, region.right() - x),
                             std::min(kUndoTileSize, region.bottom() - y)};
            if (!differs(before, after, area)) continue;
            Tile tile{area, before.copy_region(area), after.copy_region(area)};
            delta.m_bytes += pixel_bytes(tile.before) + pixel_bytes(tile.after);
            delta.m_tiles.push_back(std::move(tile));
            delta.m_bounds = delta.m_bounds.united(area);
        }
    }
    return delta;
}

void PixelDelta::append(PixelDelta&& other) {
    m_bounds = m_bounds.united(other.m_bounds);
    m_bytes += other.m_bytes;
    m_tiles.insert(m_tiles.end(), std::make_move_iterator(other.m_tiles.begin()),
                   std::make_move_iterator(other.m_tiles.end()));
}

void PixelDelta::translate(int dx, int dy) {
    for (Tile& tile : m_tiles) {
        tile.area.x += dx;
        tile.area.y += dy;
    }
    m_bounds.x += dx;
    m_bounds.y += dy;
}

void PixelDelta::apply(PixelBuffer& target, const Tile& tile, const PixelBuffer& pixels) {
    target.blit(tile.area.x, tile.area.y, pixels);
}

void PixelDelta::undo(PixelBuffer& target) const {
    for (const Tile& tile : m_tiles) apply(target, tile, tile.before);
}

void PixelDelta::redo(PixelBuffer& target) const {
    for (const Tile& tile : m_tiles) apply(target, tile, tile.after);
}

}  // namespace lsp
