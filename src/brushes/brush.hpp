#pragma once

namespace lsp {

enum class BrushShape { Round, Square };

struct Brush {
    int size = 1;
    float spacing = 0.25f;
    BrushShape shape = BrushShape::Round;
};

}  // namespace lsp
