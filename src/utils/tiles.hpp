#pragma once

#include "core/types.hpp"

namespace lsp {

// Calls `visit(column, row)` for every square of `tile_size` pixels a side that
// `region` touches. The region must lie on the grid.
template <typename VisitFn>
void for_each_tile(const RectI& region, int tile_size, VisitFn visit) {
    if (region.empty()) return;
    for (int row = region.y / tile_size; row <= (region.bottom() - 1) / tile_size; ++row)
        for (int column = region.x / tile_size; column <= (region.right() - 1) / tile_size; ++column)
            visit(column, row);
}

}  // namespace lsp
