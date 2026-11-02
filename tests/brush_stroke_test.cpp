#include <array>
#include <cstdio>

#include "tools/pencil_tool.hpp"
#include "tools/tool_context.hpp"

using namespace lsp;

namespace {
int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; \
} } while (0)

struct Fixture {
    CanvasDocument doc{128, 128};
    HistoryStack history{doc};
    Brush brush{3, 0.25f, BrushShape::Round};
    ColorPair colors{{0, 0, 0, 255}, kWhite};
    SelectionShape shape = SelectionShape::Rectangular;
    ShapeOptions shape_options;
    TextOptions text_options;
    ToolUiState tool_ui;
    ToolContext ctx{doc, history, brush, colors, shape, shape_options, text_options, tool_ui};
    PencilTool tool;
};

void curved_stroke_preserves_endpoints_and_undo() {
    Fixture f;
    const PixelBuffer before = f.doc.active_layer().pixels;
    f.tool.on_pointer_down(f.ctx, {20.5f, 60.5f}, PointerButton::Primary);
    f.tool.on_pointer_move(f.ctx, {60.5f, 20.5f});
    f.tool.on_pointer_move(f.ctx, {100.5f, 60.5f});
    CHECK(f.doc.active_layer().pixels.at(40, 35).r == 0);
    CHECK(f.doc.active_layer().pixels.at(40, 40) == kWhite);
    f.tool.on_pointer_up(f.ctx, {100.5f, 60.5f});
    const PixelBuffer painted = f.doc.active_layer().pixels;
    CHECK(painted.at(20, 60).r == 0);
    CHECK(painted.at(60, 20).r == 0);
    CHECK(painted.at(100, 60).r == 0);
    f.history.undo();
    CHECK(f.doc.active_layer().pixels == before);
    f.history.redo();
    CHECK(f.doc.active_layer().pixels == painted);
}

void clicks_lines_duplicates_and_reuse() {
    Fixture f;
    f.tool.on_pointer_down(f.ctx, {10.5f, 10.5f}, PointerButton::Primary);
    f.tool.on_pointer_up(f.ctx, {10.5f, 10.5f});
    CHECK(f.doc.active_layer().pixels.at(10, 10).r == 0);
    f.tool.on_pointer_down(f.ctx, {20.5f, 30.5f}, PointerButton::Primary);
    f.tool.on_pointer_move(f.ctx, {20.5f, 30.5f});
    f.tool.on_pointer_up(f.ctx, {100.5f, 30.5f});
    for (int x = 20; x <= 100; ++x) CHECK(f.doc.active_layer().pixels.at(x, 30).r == 0);
    CHECK(f.doc.active_layer().pixels.at(50, 25) == kWhite);
}

void uneven_samples_reversals_and_cancel() {
    Fixture f;
    const PixelBuffer before = f.doc.active_layer().pixels;
    f.tool.on_pointer_down(f.ctx, {10.5f, 10.5f}, PointerButton::Primary);
    for (Point2f point : std::array{Point2f{10.5f, 10.5f}, Point2f{10.51f, 10.5f},
                                  Point2f{110.5f, 90.5f}, Point2f{110.5f, 90.5f},
                                  Point2f{10.51f, 10.5f}, Point2f{10.5f, 10.5f}}) {
        f.tool.on_pointer_move(f.ctx, point);
    }
    CHECK(f.tool.on_cancel(f.ctx));
    CHECK(f.doc.active_layer().pixels == before);
    CHECK(!f.tool.on_cancel(f.ctx));
    f.tool.on_pointer_down(f.ctx, {60.5f, 60.5f}, PointerButton::Primary);
    f.tool.on_pointer_up(f.ctx, {60.5f, 60.5f});
    CHECK(f.doc.active_layer().pixels.at(60, 60).r == 0);
}

void brush_names_round_trip() {
    for (int i = 0; i <= static_cast<int>(BrushKind::Liquify); ++i) {
        const auto kind = static_cast<BrushKind>(i);
        CHECK(brush::from_name(brush::name(kind)) == kind);
    }
    CHECK(!brush::from_name("").has_value());
    CHECK(!brush::from_name("unknown").has_value());
}
}  // namespace

int main() {
    curved_stroke_preserves_endpoints_and_undo();
    clicks_lines_duplicates_and_reuse();
    uneven_samples_reversals_and_cancel();
    brush_names_round_trip();
    if (failures) return 1;
    std::puts("All brush stroke tests passed");
}
