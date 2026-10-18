#include "tools/fill_tool.hpp"

#include <utility>
#include <vector>

#include "selection/selection_mask.hpp"
#include "tools/stroke_recorder.hpp"
#include "tools/tool_context.hpp"
#include "utils/math.hpp"

namespace lsp {

void FillTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    PixelBuffer& pixels = context.document.active_layer().pixels;
    const int start_x = floor_to_int(position.x);
    const int start_y = floor_to_int(position.y);
    if (!pixels.contains(start_x, start_y)) return;

    const SelectionMask& selection = context.document.selection();
    if (!selection.contains(start_x, start_y)) return;

    const Rgba8 fill = context.colors.ink(button);
    const Rgba8 target = pixels.at(start_x, start_y);
    if (target == fill) return;

    const auto fillable = [&](int x, int y) { return pixels.at(x, y) == target && selection.contains(x, y); };

    StrokeRecorder recorder(context.document);
    RectI dirty;
    std::vector<std::pair<int, int>> seeds{{start_x, start_y}};

    while (!seeds.empty()) {
        const auto [seed_x, y] = seeds.back();
        seeds.pop_back();
        if (!fillable(seed_x, y)) continue;

        int left = seed_x;
        int right = seed_x;
        while (left > 0 && fillable(left - 1, y)) --left;
        while (right + 1 < pixels.width() && fillable(right + 1, y)) ++right;

        for (int x = left; x <= right; ++x) pixels.at(x, y) = fill;
        dirty = dirty.united({left, y, right - left + 1, 1});

        for (const int next_y : {y - 1, y + 1}) {
            if (next_y < 0 || next_y >= pixels.height()) continue;
            bool in_span = false;
            for (int x = left; x <= right; ++x) {
                const bool match = fillable(x, next_y);
                if (match && !in_span) seeds.emplace_back(x, next_y);
                in_span = match;
            }
        }
    }

    recorder.touch(dirty);
    recorder.commit(context.history);
}

}  // namespace lsp
