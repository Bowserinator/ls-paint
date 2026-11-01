#pragma once

#include <string>

#include "core/pixel_buffer.hpp"

namespace lsp {

struct Layer {
    std::string name;
    PixelBuffer pixels;
};

}  // namespace lsp
