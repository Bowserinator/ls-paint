#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "core/types.hpp"

namespace lsp {

// The x positions, left to right, where the polygon's edges cross the
// horizontal line at `scan_y`. Pairs of them bound the interior. An edge counts
// from its lower end up to but not including its upper end, so a vertex that
// sits on the line is counted once.
inline void polygon_crossings(std::span<const Point2f> points, float scan_y, std::vector<float>& out) {
    out.clear();
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point2f& p = points[i];
        const Point2f& q = points[(i + 1) % points.size()];
        if ((p.y <= scan_y) == (q.y <= scan_y)) continue;
        out.push_back(p.x + (scan_y - p.y) / (q.y - p.y) * (q.x - p.x));
    }
    std::ranges::sort(out);
}

}  // namespace lsp
