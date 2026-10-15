#pragma once

#include "core/layer_stack.hpp"
#include "core/pixel_buffer.hpp"

namespace lsp {

void composite(const LayerStack& layers, RectI region, PixelBuffer& target);

}  // namespace lsp
