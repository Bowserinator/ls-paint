#pragma once

#include "core/types.hpp"

namespace lsp {

enum class BlendMode { Normal, Multiply };

using BlendFn = Rgba8 (*)(Rgba8 base, Rgba8 top, float opacity);

[[nodiscard]] BlendFn blend_fn_for(BlendMode mode);

}  // namespace lsp
