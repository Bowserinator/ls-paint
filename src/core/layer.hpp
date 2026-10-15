#pragma once

#include <string>

#include "core/blend_modes.hpp"
#include "core/pixel_buffer.hpp"

namespace lsp {

struct Layer {
    std::string name;
    PixelBuffer pixels;
    float opacity = 1.0f;
    BlendMode blend_mode = BlendMode::Normal;
    bool visible = true;
};

}  // namespace lsp
