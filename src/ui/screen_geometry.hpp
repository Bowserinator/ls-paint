#pragma once

#include <vector>

#include "core/types.hpp"

namespace lsp {

struct ScreenGeometry {
    std::vector<RectI> work_areas;
    bool position_supported = true;
};

[[nodiscard]] ScreenGeometry screen_geometry(float window_scale);

}  // namespace lsp
