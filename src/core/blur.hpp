#pragma once

#include "core/pixel_buffer.hpp"

namespace lsp {

namespace blur {

// A box blur applied twice, which tracks a Gaussian closely enough for a soft
// brush. The colour of a transparent pixel is weighted by its alpha while the
// blur runs, so it cannot drag its neighbours toward black. A radius below one
// gives the source back unchanged.
[[nodiscard]] PixelBuffer box_twice(const PixelBuffer& source, int radius);

}  // namespace blur

}  // namespace lsp
