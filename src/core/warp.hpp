#pragma once

#include "core/pixel_buffer.hpp"
#include "core/types.hpp"

namespace lsp::warp {

// Reads a picture at a fractional position, averaging its four neighbours.
// Positions outside the picture take the nearest edge pixel, and colour is
// weighted by alpha so that pulling a soft edge in cannot darken it.
[[nodiscard]] Rgba8 sample_bilinear(const PixelBuffer& source, float x, float y) noexcept;

}  // namespace lsp::warp
