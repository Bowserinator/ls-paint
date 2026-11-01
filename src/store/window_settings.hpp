#pragma once

#include <span>

#include "core/types.hpp"
#include "store/store.hpp"

namespace lsp {

// Restores reachable window geometry and saves any repaired settings.
[[nodiscard]] RectI restore_window_settings(Store& store, RectI defaults,
                                            std::span<const RectI> work_areas,
                                            bool position_supported = true);

}  // namespace lsp
