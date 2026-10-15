#pragma once

#include "core/types.hpp"

namespace lsp {

struct ToolContext;

enum class ToolId {
    None, Selection, Resize, Rotate,
    Pencil, Fill, Text, Eraser, Dropper, Magnify
};

enum class PointerButton { Primary, Secondary };

enum class SelectionShape { Rectangular, Freehand, Polygon };

class ITool {
public:
    virtual ~ITool() = default;

    virtual void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) = 0;
    virtual void on_pointer_move(ToolContext&, Point2f) {}
    virtual void on_pointer_up(ToolContext&, Point2f) {}
};

}  // namespace lsp
