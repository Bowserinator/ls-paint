#pragma once

#include <string_view>

#include "core/types.hpp"

namespace lsp {

struct ToolContext;

enum class ToolId {
    None, Selection, Resize, Rotate,
    Pencil, Brush, Fill, Shape, Text, Eraser, Dropper, Magnify
};

enum class PointerButton { Primary, Secondary };

enum class SelectionShape { Rectangular, Freehand, Polygon };

class ITool {
public:
    virtual ~ITool() = default;

    virtual void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) = 0;
    virtual void on_pointer_move(ToolContext&, Point2f) {}
    virtual void on_pointer_up(ToolContext&, Point2f) {}
    // Enter/Escape while no gesture is in flight.
    virtual void on_key_pressed(ToolContext&, std::string_view /*text*/) {}
    virtual void on_double_click(ToolContext&) {}
    // The tool was deselected: release anything transient it was holding.
    virtual void on_deactivate(ToolContext&) {}
};

}  // namespace lsp
