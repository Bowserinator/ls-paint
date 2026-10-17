#include <cstdio>

#include "app/app_controller.hpp"
#include "history/commands/selection_edit_command.hpp"
#include "tools/eraser_tool.hpp"
#include "tools/pencil_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool_context.hpp"

using namespace lsp;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

struct Fixture {
    CanvasDocument doc;
    HistoryStack history;
    Brush brush{4, 0.25f, BrushShape::Round};
    ColorPair colors{{0, 0, 0, 255}, {255, 255, 255, 255}};
    SelectionShape shape = SelectionShape::Rectangular;
    ShapeOptions shape_options;
    TextOptions text_options;
    ToolUiState tool_ui;
    ToolContext ctx{doc, history, brush, colors, shape, shape_options, text_options, tool_ui};

    explicit Fixture(int width = 32, int height = 32) : doc(width, height), history(doc) {}
};

void stroke(ITool& tool, Fixture& fixture, Point2f from, Point2f to, PointerButton button = PointerButton::Primary) {
    tool.on_pointer_down(fixture.ctx, from, button);
    tool.on_pointer_move(fixture.ctx, to);
    tool.on_pointer_up(fixture.ctx, to);
    fixture.doc.flush();
}

// A degenerate marquee used to allocate an all-zero mask: active() said
// "selection" while contains() was false everywhere, so nothing could be drawn.
void test_degenerate_marquee_leaves_canvas_paintable() {
    Fixture fixture;
    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {10, 10}, PointerButton::Primary);
    tool.on_pointer_up(fixture.ctx, {10, 10});
    CHECK(!fixture.doc.selection().active());
    CHECK(fixture.doc.selection().bounds().empty());

    PencilTool pencil;
    stroke(pencil, fixture, {5, 5}, {20, 5});
    CHECK((fixture.doc.active_layer().pixels.at(10, 5) == Rgba8{0, 0, 0, 255}));
}

void test_fewer_than_three_polygon_points_leaves_no_selection() {
    Fixture fixture;
    fixture.doc.selection().fill_polygon({{2, 2}, {6, 6}});
    CHECK(!fixture.doc.selection().active());
    CHECK(fixture.doc.selection().bounds().empty());
}

void test_selection_clips_drawing_to_its_bounds() {
    Fixture fixture;
    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {4, 4}, PointerButton::Primary);
    tool.on_pointer_move(fixture.ctx, {12, 12});
    tool.on_pointer_up(fixture.ctx, {12, 12});
    CHECK((fixture.doc.selection().bounds() == RectI{4, 4, 8, 8}));

    PencilTool pencil;
    stroke(pencil, fixture, {6, 6}, {10, 6});
    CHECK((fixture.doc.active_layer().pixels.at(8, 6) == Rgba8{0, 0, 0, 255}));
    CHECK((fixture.doc.active_layer().pixels.at(20, 6) == Rgba8{255, 255, 255, 255}));
}

// A transparent ink color painted invisible pixels while the stroke still ran,
// which read as a dead pencil.
void test_transparent_ink_is_treated_as_opaque() {
    Fixture fixture;
    fixture.colors.primary = {255, 255, 255, 0};
    CHECK((fixture.colors.ink(PointerButton::Primary) == Rgba8{255, 255, 255, 255}));

    PencilTool pencil;
    stroke(pencil, fixture, {5, 5}, {20, 5});
    CHECK(fixture.doc.active_layer().pixels.at(10, 5).a == 255);
}

void test_eraser_paints_the_secondary_color_inside_the_selection() {
    Fixture fixture;
    fixture.colors.secondary = {255, 0, 0, 255};
    PencilTool pencil;
    stroke(pencil, fixture, {5, 5}, {25, 5});

    SelectionTool selection;
    selection.on_pointer_down(fixture.ctx, {4, 4}, PointerButton::Primary);
    selection.on_pointer_move(fixture.ctx, {12, 12});
    selection.on_pointer_up(fixture.ctx, {12, 12});

    EraserTool eraser;
    stroke(eraser, fixture, {5, 6}, {20, 6});
    CHECK((fixture.doc.active_layer().pixels.at(8, 6) == Rgba8{255, 0, 0, 255}));
    CHECK((fixture.doc.active_layer().pixels.at(16, 6) == Rgba8{0, 0, 0, 255}));
}

void test_cut_copy_paste_round_trip() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {10, 20, 30, 255};

    document.selection().fill_rect({4, 4, 4, 4});
    app.copy_selection();
    CHECK(!app.clipboard().empty());

    app.colors().secondary = {1, 2, 3, 255};  // Color 2 is what shows through the hole
    app.delete_selection_contents();
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{1, 2, 3, 255}));
    CHECK(document.selection().active());  // deleting keeps the selection, like Cut

    app.clear_selection();
    app.paste_clipboard();

    // The pasted block hovers over the canvas, so the layer keeps its pixels
    // until the paste is applied.
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(1, 1) == Rgba8{255, 255, 255, 255}));
    CHECK((document.selection().bounds() == RectI{0, 0, 4, 4}));

    app.clear_selection();  // deselecting applies the paste, like a click outside
    CHECK(!document.floating().has_value());
    CHECK(!document.selection().active());
    CHECK((document.active_layer().pixels.at(1, 1) == Rgba8{10, 20, 30, 255}));

    app.history().undo();  // undo applying it: the block hovers again
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(1, 1) == Rgba8{255, 255, 255, 255}));

    app.history().undo();  // undo paste
    CHECK(!document.floating().has_value());
    CHECK(!document.selection().active());

    app.history().undo();  // undo delete
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{10, 20, 30, 255}));

    app.history().undo();  // nothing left
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{10, 20, 30, 255}));
}

// A pasted block is a floating selection: it can still be moved before it is
// applied, and the layer keeps its pixels until then.
void test_paste_hovers_and_can_move_before_it_lands() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {10, 20, 30, 255};
    document.selection().fill_rect({4, 4, 4, 4});
    app.copy_selection();
    app.clear_selection();

    app.paste_clipboard();
    CHECK(document.floating().has_value());

    lsp::translate_contents(document.selection(), *document.floating(), 10, 0);
    CHECK((document.selection().bounds() == RectI{10, 0, 4, 4}));
    CHECK((document.active_layer().pixels.at(11, 1) == Rgba8{255, 255, 255, 255}));

    app.clear_selection();  // deselecting is what applies it
    CHECK((document.active_layer().pixels.at(11, 1) == Rgba8{10, 20, 30, 255}));
}

void test_paste_can_be_resized_before_it_lands() {
    AppController app;
    CanvasDocument& document = app.document();
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) document.active_layer().pixels.at(4 + x, 4 + y) = {10, 20, 30, 255};
    document.selection().fill_rect({4, 4, 4, 4});
    app.copy_selection();
    app.clear_selection();

    app.paste_clipboard();
    CHECK(document.floating().has_value());

    (void)lsp::resize_selection(document.selection(), document.floating(), RectI{0, 0, 8, 8});
    CHECK((document.floating()->content.width() == 8));
    CHECK((document.selection().bounds() == RectI{0, 0, 8, 8}));
    CHECK((document.active_layer().pixels.at(1, 1) == Rgba8{255, 255, 255, 255}));

    app.clear_selection();  // deselecting is what applies it
    int painted = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (document.active_layer().pixels.at(x, y) == Rgba8{10, 20, 30, 255}) ++painted;
    CHECK(painted == 64);  // the block grew to the resized bounds
}

void test_paste_without_a_clipboard_is_a_no_op() {
    AppController app;
    CanvasDocument& document = app.document();
    CHECK(app.clipboard().empty());
    app.paste_clipboard();
    CHECK(!document.selection().active());
}

void test_paste_landing_entirely_off_canvas_is_a_no_op() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {10, 20, 30, 255};
    document.selection().fill_rect({4, 4, 4, 4});
    const ClipboardContent clip = capture_clipboard(document.active_layer().pixels, document.selection());
    app.clear_selection();

    const SelectionEdit edit = lsp::paste_clipboard(document.active_layer().pixels, document.selection(),
                                                    clip, 1000, 1000);
    CHECK(edit.empty());
    CHECK(!document.selection().active());
}

void test_rotate_selection_contents_keeps_the_mask_aligned() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(4, 4) = {1, 2, 3, 255};
    document.selection().fill_rect({4, 4, 4, 2});

    app.transform_selection_contents(ContentTransform::RotateCw90);

    CHECK((document.selection().bounds() == RectI{5, 3, 2, 4}));
    CHECK(document.floating().has_value());
    // The layer itself is untouched: the rotated copy floats above it, and the
    // background color only shows where the copy was lifted from.
    CHECK((document.active_layer().pixels.at(4, 4) == Rgba8{1, 2, 3, 255}));
    CHECK((document.active_layer().pixels.at(6, 3) == Rgba8{255, 255, 255, 255}));
    (void)document.flush();
    CHECK((document.flattened().at(6, 3) == Rgba8{1, 2, 3, 255}));
    CHECK((document.flattened().at(4, 4) == Rgba8{255, 255, 255, 255}));

    app.history().undo();
    (void)document.flush();
    CHECK((document.selection().bounds() == RectI{4, 4, 4, 2}));
    CHECK(!document.floating().has_value());
    CHECK((document.flattened().at(4, 4) == Rgba8{1, 2, 3, 255}));
    CHECK((document.flattened().at(6, 3) == Rgba8{255, 255, 255, 255}));
}

// Rotating twice must be a 180° turn of the originals. Doing it by lifting and
// pasting twice instead piles background color into the corners, so the
// rotation stays pending until the selection is applied.
void test_rotating_twice_turns_the_originals_by_180() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(4, 4) = {1, 2, 3, 255};
    document.active_layer().pixels.at(7, 4) = {7, 7, 7, 255};
    document.selection().fill_rect({4, 4, 4, 2});

    app.transform_selection_contents(ContentTransform::RotateCw90);
    app.transform_selection_contents(ContentTransform::RotateCw90);

    CHECK((document.selection().bounds() == RectI{4, 4, 4, 2}));
    // Still nothing written: the buffer holds the pristine originals.
    CHECK((document.active_layer().pixels.at(4, 4) == Rgba8{1, 2, 3, 255}));
    CHECK((document.active_layer().pixels.at(7, 4) == Rgba8{7, 7, 7, 255}));

    // What the window shows really is the originals turned by 180°.
    (void)document.flush();
    CHECK((document.flattened().at(7, 5) == Rgba8{1, 2, 3, 255}));
    CHECK((document.flattened().at(4, 5) == Rgba8{7, 7, 7, 255}));
    CHECK((document.flattened().at(4, 4) == Rgba8{255, 255, 255, 255}));

    // Applying it writes that result exactly once.
    app.clear_selection();
    CHECK(!document.floating().has_value());
    CHECK((document.active_layer().pixels.at(7, 5) == Rgba8{1, 2, 3, 255}));
    CHECK((document.active_layer().pixels.at(4, 5) == Rgba8{7, 7, 7, 255}));
}

void test_flip_selection_contents_horizontal() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(4, 4) = {1, 2, 3, 255};
    document.active_layer().pixels.at(7, 4) = {7, 7, 7, 255};
    document.selection().fill_rect({4, 4, 4, 2});

    app.transform_selection_contents(ContentTransform::FlipHorizontal);

    CHECK((document.selection().bounds() == RectI{4, 4, 4, 2}));
    CHECK((document.active_layer().pixels.at(4, 4) == Rgba8{1, 2, 3, 255}));  // layer untouched
    (void)document.flush();
    CHECK((document.flattened().at(7, 4) == Rgba8{1, 2, 3, 255}));
    CHECK((document.flattened().at(4, 4) == Rgba8{7, 7, 7, 255}));

    app.history().undo();
    (void)document.flush();
    CHECK((document.flattened().at(4, 4) == Rgba8{1, 2, 3, 255}));
    CHECK((document.flattened().at(7, 4) == Rgba8{7, 7, 7, 255}));
}

void test_moving_selection_contents_and_undo() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {9, 9, 9, 255};
    document.selection().fill_rect({4, 4, 4, 4});

    lift_contents(document.active_layer().pixels, document.selection(), document.floating(),
                  {0, 128, 255, 255});
    translate_contents(document.selection(), *document.floating(), 10, 0);
    document.mark_dirty(document.bounds());

    CHECK((document.selection().bounds() == RectI{14, 4, 4, 4}));
    // The move only rearranges the floating copy: the layer still holds the
    // originals, and nothing was painted on the way over.
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(15, 5) == Rgba8{255, 255, 255, 255}));
    CHECK((document.active_layer().pixels.at(10, 5) == Rgba8{255, 255, 255, 255}));
    (void)document.flush();
    CHECK((document.flattened().at(15, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.flattened().at(5, 5) == Rgba8{0, 128, 255, 255}));  // the source shows Color 2

    SelectionEdit edit =
        commit_contents(document.active_layer().pixels, document.selection(), document.floating());
    CHECK(!edit.empty());
    CHECK(!document.floating().has_value());
    CHECK((document.active_layer().pixels.at(15, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{0, 128, 255, 255}));

    // Undoing the apply hands the contents back as a float: they are not
    // written to the layer a second time.
    app.history().push(make_selection_edit(document.layers().active_index(), edit));
    app.history().undo();

    CHECK((document.selection().bounds() == RectI{14, 4, 4, 4}));
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(15, 5) == Rgba8{255, 255, 255, 255}));
    CHECK((document.active_layer().pixels.at(10, 5) == Rgba8{255, 255, 255, 255}));
}

// Moving in several steps never touches the layer, so the pixels the selection
// was dragged across stay exactly as they were.
void test_moving_in_steps_never_paints_the_swept_region() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {9, 9, 9, 255};
    document.selection().fill_rect({4, 4, 4, 4});

    lift_contents(document.active_layer().pixels, document.selection(), document.floating(),
                  {0, 128, 255, 255});
    translate_contents(document.selection(), *document.floating(), 5, 0);
    translate_contents(document.selection(), *document.floating(), 15, 0);

    CHECK((document.selection().bounds() == RectI{24, 4, 4, 4}));
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(10, 5) == Rgba8{255, 255, 255, 255}));  // swept, never painted
    CHECK((document.active_layer().pixels.at(25, 5) == Rgba8{255, 255, 255, 255}));  // not painted yet

    SelectionEdit edit =
        commit_contents(document.active_layer().pixels, document.selection(), document.floating());
    CHECK(!edit.empty());
    CHECK((document.active_layer().pixels.at(25, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{0, 128, 255, 255}));
    CHECK((document.active_layer().pixels.at(10, 5) == Rgba8{255, 255, 255, 255}));

    app.history().push(make_selection_edit(document.layers().active_index(), edit));
    app.history().undo();

    CHECK((document.selection().bounds() == RectI{24, 4, 4, 4}));
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((document.active_layer().pixels.at(10, 5) == Rgba8{255, 255, 255, 255}));
    CHECK((document.active_layer().pixels.at(25, 5) == Rgba8{255, 255, 255, 255}));
}

// Right-drag used to be how a polygon was finished; left-click near the first
// vertex now closes it, and a click that closes nothing leaves no selection.
void test_polygon_closes_on_a_click_near_the_first_point() {
    Fixture fixture;
    fixture.shape = SelectionShape::Polygon;
    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {4, 4}, PointerButton::Primary);
    tool.on_pointer_down(fixture.ctx, {12, 4}, PointerButton::Primary);
    tool.on_pointer_down(fixture.ctx, {12, 12}, PointerButton::Primary);
    CHECK(!fixture.doc.selection().active());
    tool.on_pointer_down(fixture.ctx, {4, 4.5f}, PointerButton::Primary);
    CHECK(fixture.doc.selection().active());
    CHECK(fixture.doc.selection().contains(8, 6));
}

// Dragging inside an active selection moves its contents instead of starting a
// new marquee, and the layer only changes when the move is applied.
void test_dragging_inside_a_selection_moves_the_contents() {
    Fixture fixture;
    fixture.doc.active_layer().pixels.at(5, 5) = {9, 9, 9, 255};
    fixture.doc.selection().fill_rect({4, 4, 4, 4});
    fixture.colors.secondary = {0, 200, 0, 255};  // Color 2 must travel from the tool context

    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {5, 5}, PointerButton::Primary);
    tool.on_pointer_move(fixture.ctx, {13, 5});
    tool.on_pointer_up(fixture.ctx, {13, 5});

    CHECK((fixture.doc.selection().bounds() == RectI{12, 4, 4, 4}));
    CHECK(fixture.doc.floating().has_value());
    // The contents were lifted, not stamped: the layer still holds the
    // originals where they always were.
    CHECK((fixture.doc.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.active_layer().pixels.at(13, 5) == Rgba8{255, 255, 255, 255}));
    (void)fixture.doc.flush();
    CHECK((fixture.doc.flattened().at(13, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.flattened().at(5, 5) == Rgba8{0, 200, 0, 255}));  // vacated, not transparent

    fixture.history.undo();
    CHECK((fixture.doc.selection().bounds() == RectI{4, 4, 4, 4}));
    CHECK(!fixture.doc.floating().has_value());
    (void)fixture.doc.flush();
    CHECK((fixture.doc.flattened().at(5, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.flattened().at(13, 5) == Rgba8{255, 255, 255, 255}));
}

// Grips recrop a selection that never left the layer: plain geometry, so the
// marquee does not earn an undo entry of its own.
void test_resizing_an_unfloated_selection_is_plain_geometry() {
    Fixture fixture;
    fixture.doc.selection().fill_rect({4, 4, 16, 16});

    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {20, 20}, PointerButton::Primary);  // the south-east grip
    tool.on_pointer_move(fixture.ctx, {24, 24});
    tool.on_pointer_up(fixture.ctx, {24, 24});

    CHECK((fixture.doc.selection().bounds() == RectI{4, 4, 21, 21}));
    CHECK(!fixture.doc.floating().has_value());

    PencilTool pencil;
    stroke(pencil, fixture, {5, 5}, {6, 5});
    CHECK((fixture.doc.active_layer().pixels.at(5, 5) == Rgba8{0, 0, 0, 255}));

    fixture.history.undo();  // only the stroke was recorded
    CHECK((fixture.doc.active_layer().pixels.at(5, 5) == Rgba8{255, 255, 255, 255}));
    CHECK((fixture.doc.selection().bounds() == RectI{4, 4, 21, 21}));
}

// A floating selection scaled by a grip keeps its own undo entry: undo hands
// back the pre-resize contents rather than a resampled copy.
void test_resizing_a_floated_selection_is_undoable() {
    Fixture fixture;
    fixture.doc.active_layer().pixels.at(5, 5) = {9, 9, 9, 255};
    fixture.doc.selection().fill_rect({4, 4, 16, 16});
    fixture.colors.secondary = {0, 200, 0, 255};

    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {8, 8}, PointerButton::Primary);
    tool.on_pointer_move(fixture.ctx, {9, 8});
    tool.on_pointer_up(fixture.ctx, {9, 8});
    CHECK(fixture.doc.floating().has_value());
    CHECK((fixture.doc.selection().bounds() == RectI{5, 4, 16, 16}));

    tool.on_pointer_down(fixture.ctx, {20, 19}, PointerButton::Primary);  // the south-east grip
    tool.on_pointer_move(fixture.ctx, {24, 23});
    tool.on_pointer_up(fixture.ctx, {24, 23});

    CHECK((fixture.doc.selection().bounds() == RectI{5, 4, 20, 20}));
    CHECK((fixture.doc.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));  // layer untouched
    (void)fixture.doc.flush();
    // 16 -> 20 nearest-neighbour puts source pixel (1,1) at target (2,2).
    CHECK((fixture.doc.flattened().at(7, 6) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.flattened().at(4, 10) == Rgba8{0, 200, 0, 255}));  // the origin still shows

    fixture.history.undo();
    CHECK((fixture.doc.selection().bounds() == RectI{5, 4, 16, 16}));
    (void)fixture.doc.flush();
    CHECK((fixture.doc.flattened().at(6, 5) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.flattened().at(7, 6) == Rgba8{255, 255, 255, 255}));
    CHECK((fixture.doc.flattened().at(4, 10) == Rgba8{0, 200, 0, 255}));
}

// Rescaling twice must sample the lifted originals both times: squashing a
// selection and stretching it back restores the pixels it started with
// instead of magnifying the squashed copy.
void test_resizing_twice_samples_from_the_original_contents() {
    Fixture fixture;
    fixture.doc.active_layer().pixels.at(3, 3) = {9, 9, 9, 255};
    fixture.doc.selection().fill_rect({2, 2, 16, 16});
    fixture.colors.secondary = {0, 200, 0, 255};

    lift_contents(fixture.doc.active_layer().pixels, fixture.doc.selection(), fixture.doc.floating(),
                  fixture.colors.ink(PointerButton::Secondary));
    CHECK(fixture.doc.floating().has_value());

    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {18, 18}, PointerButton::Primary);  // the south-east grip
    tool.on_pointer_move(fixture.ctx, {9, 9});
    tool.on_pointer_up(fixture.ctx, {9, 9});
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 8, 8}));
    (void)fixture.doc.flush();
    // 16 -> 8 skips the marker pixel entirely, so the squash really thinned
    // the contents.
    CHECK((fixture.doc.flattened().at(3, 3) == Rgba8{255, 255, 255, 255}));

    tool.on_pointer_down(fixture.ctx, {9, 9}, PointerButton::Primary);  // still a grip at 8x8
    tool.on_pointer_move(fixture.ctx, {17, 17});
    tool.on_pointer_up(fixture.ctx, {17, 17});
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 16, 16}));
    CHECK((fixture.doc.active_layer().pixels.at(3, 3) == Rgba8{9, 9, 9, 255}));  // layer untouched
    (void)fixture.doc.flush();
    CHECK((fixture.doc.flattened().at(3, 3) == Rgba8{9, 9, 9, 255}));

    fixture.history.undo();  // the stretch-back goes away first
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 8, 8}));
}

// A rotation between two rescales must not become the new starting point
// either: squash, rotate and stretch back still shows the pixels the lift
// began with, because the turn runs on the lifted originals.
void test_transforming_a_squashed_selection_still_rescales_from_the_original() {
    Fixture fixture;
    fixture.doc.active_layer().pixels.at(3, 3) = {9, 9, 9, 255};
    fixture.doc.selection().fill_rect({2, 2, 16, 16});
    fixture.colors.secondary = {0, 200, 0, 255};

    lift_contents(fixture.doc.active_layer().pixels, fixture.doc.selection(), fixture.doc.floating(),
                  fixture.colors.ink(PointerButton::Secondary));

    SelectionTool tool;
    tool.on_pointer_down(fixture.ctx, {18, 18}, PointerButton::Primary);  // the south-east grip
    tool.on_pointer_move(fixture.ctx, {9, 9});
    tool.on_pointer_up(fixture.ctx, {9, 9});
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 8, 8}));
    (void)fixture.doc.flush();
    CHECK((fixture.doc.flattened().at(3, 3) == Rgba8{255, 255, 255, 255}));  // squashed thin

    (void)transform_contents(fixture.doc.active_layer().pixels, fixture.doc.selection(),
                             fixture.doc.floating(), ContentTransform::RotateCw90,
                             fixture.colors.ink(PointerButton::Secondary));
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 8, 8}));  // a turn keeps the size

    tool.on_pointer_down(fixture.ctx, {9, 9}, PointerButton::Primary);
    tool.on_pointer_move(fixture.ctx, {17, 17});
    tool.on_pointer_up(fixture.ctx, {17, 17});
    CHECK((fixture.doc.selection().bounds() == RectI{2, 2, 16, 16}));
    CHECK((fixture.doc.active_layer().pixels.at(3, 3) == Rgba8{9, 9, 9, 255}));  // layer untouched
    (void)fixture.doc.flush();
    // The marker rode through the turn: (1,1) of 16 becomes (14,1), doc (16,3).
    CHECK((fixture.doc.flattened().at(16, 3) == Rgba8{9, 9, 9, 255}));
    CHECK((fixture.doc.flattened().at(3, 3) == Rgba8{255, 255, 255, 255}));
}

// Starting a stroke with another tool applies the pending float first, because
// the stroke belongs to the layer underneath it.
void test_switching_tools_commits_the_floating_selection() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {1, 2, 3, 255};
    document.selection().fill_rect({4, 4, 4, 4});
    app.colors().secondary = {9, 9, 9, 255};

    app.transform_selection_contents(ContentTransform::RotateCw90);
    CHECK(document.floating().has_value());

    app.input().pointer_down(ToolId::Pencil, {20, 20}, PointerButton::Primary);
    app.input().pointer_up({20, 20});

    CHECK(!document.floating().has_value());
    CHECK((document.active_layer().pixels.at(6, 5) == Rgba8{1, 2, 3, 255}));
    CHECK((document.active_layer().pixels.at(4, 4) == Rgba8{255, 255, 255, 255}));  // covered by the copy

    app.history().undo();
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{1, 2, 3, 255}));
    CHECK((document.active_layer().pixels.at(6, 5) == Rgba8{255, 255, 255, 255}));
}

// Copying while contents are lifted reads the float; reading the layer instead
// would hand back the originals, rotation included.
void test_copying_a_floated_selection_reads_the_float() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {10, 20, 30, 255};
    document.selection().fill_rect({4, 4, 4, 4});

    app.transform_selection_contents(ContentTransform::RotateCw90);
    app.copy_selection();

    CHECK(!app.clipboard().empty());
    CHECK((app.clipboard().pixels.at(2, 1) == Rgba8{10, 20, 30, 255}));   // carried by the rotation
    CHECK((app.clipboard().pixels.at(1, 1) == Rgba8{255, 255, 255, 255}));  // the layer never rotated
}

// Cutting lifted contents takes them from the float and fills only the origin
// they came from; everything the float hovered over in the meantime survives.
void test_cutting_while_floated_fills_the_origin_only() {
    AppController app;
    CanvasDocument& document = app.document();
    document.active_layer().pixels.at(5, 5) = {10, 20, 30, 255};
    document.active_layer().pixels.at(15, 5) = {1, 2, 3, 255};  // where the contents will hover
    document.selection().fill_rect({4, 4, 4, 4});
    app.colors().secondary = {9, 9, 9, 255};

    lift_contents(document.active_layer().pixels, document.selection(), document.floating(),
                  app.colors().ink(PointerButton::Secondary));
    translate_contents(document.selection(), *document.floating(), 10, 0);
    CHECK((document.selection().bounds() == RectI{14, 4, 4, 4}));

    app.cut_selection();

    CHECK(!document.floating().has_value());
    CHECK(document.selection().active());  // cutting keeps the selection, like mspaint
    CHECK(!app.clipboard().empty());
    CHECK((app.clipboard().pixels.at(1, 1) == Rgba8{10, 20, 30, 255}));  // taken from the float
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{9, 9, 9, 255}));   // the origin is filled
    CHECK((document.active_layer().pixels.at(15, 5) == Rgba8{1, 2, 3, 255}));  // untouched

    app.history().undo();
    CHECK(document.floating().has_value());
    CHECK((document.active_layer().pixels.at(5, 5) == Rgba8{10, 20, 30, 255}));
    CHECK((document.active_layer().pixels.at(15, 5) == Rgba8{1, 2, 3, 255}));
}

// A stroke the selection clips away entirely paints nothing, so it must not
// take up an undo step either.
void test_a_fully_clipped_stroke_leaves_no_undo_entry() {
    Fixture fixture;
    PencilTool pencil;
    stroke(pencil, fixture, {1, 1}, {3, 1});
    CHECK((fixture.doc.active_layer().pixels.at(2, 1) == Rgba8{0, 0, 0, 255}));

    fixture.doc.selection().fill_rect({4, 4, 8, 8});
    stroke(pencil, fixture, {20, 20}, {24, 20});
    CHECK((fixture.doc.active_layer().pixels.at(22, 20) == Rgba8{255, 255, 255, 255}));

    fixture.history.undo();  // the stroke inside the selection, one step back
    CHECK((fixture.doc.active_layer().pixels.at(2, 1) == Rgba8{255, 255, 255, 255}));
}

// Geometry commands carry the selection so undo does not silently drop it.
void test_resize_undo_restores_the_selection() {
    AppController app;
    CanvasDocument& document = app.document();
    document.selection().fill_rect({1, 1, 5, 5});

    app.resize_canvas(64, 64, {255, 255, 255, 255});
    CHECK(!document.selection().active());
    CHECK((document.bounds() == RectI{0, 0, 64, 64}));

    app.history().undo();
    CHECK((document.bounds() == RectI{0, 0, 800, 600}));
    CHECK((document.selection().bounds() == RectI{1, 1, 5, 5}));
}

void test_select_all_and_clear_selection() {
    AppController app;
    CanvasDocument& document = app.document();
    CHECK(!document.selection().active());
    app.select_all();
    CHECK((document.selection().bounds() == document.bounds()));
    app.clear_selection();
    CHECK(!document.selection().active());
}

}  // namespace

// The lasso outlines itself while it is still being drawn: the tool publishes
// the path traced so far, and drops it once the stroke lands.
void test_freehand_gesture_publishes_its_path() {
    Fixture fixture;
    fixture.shape = SelectionShape::Freehand;
    SelectionTool tool;

    tool.on_pointer_down(fixture.ctx, {4, 4}, PointerButton::Primary);
    CHECK(fixture.tool_ui.selection.points.size() == 1);
    tool.on_pointer_move(fixture.ctx, {10, 10});
    tool.on_pointer_move(fixture.ctx, {20, 14});
    CHECK(fixture.tool_ui.selection.points.size() == 3);
    CHECK(fixture.tool_ui.selection.points.front().x == 4.0f);
    CHECK(fixture.tool_ui.selection.points.back().y == 14.0f);

    tool.on_pointer_up(fixture.ctx, {20, 14});
    CHECK(fixture.tool_ui.selection.points.empty());
    CHECK(fixture.doc.selection().active());
}

// A polygon in progress previews the edge the next click would place, and
// Escape abandons the whole gesture without touching the selection.
void test_polygon_gesture_previews_and_escape_abandons_it() {
    Fixture fixture;
    fixture.shape = SelectionShape::Polygon;
    SelectionTool tool;

    tool.on_pointer_down(fixture.ctx, {4, 4}, PointerButton::Primary);
    tool.on_pointer_down(fixture.ctx, {20, 4}, PointerButton::Primary);
    CHECK(fixture.tool_ui.selection.points.size() == 2);

    tool.on_pointer_move(fixture.ctx, {20, 20});
    CHECK(fixture.tool_ui.selection.points.size() == 3);
    CHECK(fixture.tool_ui.selection.points.back().y == 20.0f);

    tool.on_key_pressed(fixture.ctx, "\x1b");
    CHECK(fixture.tool_ui.selection.points.empty());

    // The abandoned points are gone: a fresh click starts a new polygon.
    tool.on_pointer_down(fixture.ctx, {8, 8}, PointerButton::Primary);
    CHECK(fixture.tool_ui.selection.points.size() == 1);
    CHECK(!fixture.doc.selection().active());
}

int main() {
    test_degenerate_marquee_leaves_canvas_paintable();
    test_fewer_than_three_polygon_points_leaves_no_selection();
    test_selection_clips_drawing_to_its_bounds();
    test_transparent_ink_is_treated_as_opaque();
    test_eraser_paints_the_secondary_color_inside_the_selection();
    test_cut_copy_paste_round_trip();
    test_paste_hovers_and_can_move_before_it_lands();
    test_paste_can_be_resized_before_it_lands();
    test_paste_without_a_clipboard_is_a_no_op();
    test_paste_landing_entirely_off_canvas_is_a_no_op();
    test_rotate_selection_contents_keeps_the_mask_aligned();
    test_rotating_twice_turns_the_originals_by_180();
    test_flip_selection_contents_horizontal();
    test_moving_selection_contents_and_undo();
    test_moving_in_steps_never_paints_the_swept_region();
    test_polygon_closes_on_a_click_near_the_first_point();
    test_dragging_inside_a_selection_moves_the_contents();
    test_resizing_an_unfloated_selection_is_plain_geometry();
    test_resizing_a_floated_selection_is_undoable();
    test_resizing_twice_samples_from_the_original_contents();
    test_transforming_a_squashed_selection_still_rescales_from_the_original();
    test_switching_tools_commits_the_floating_selection();
    test_copying_a_floated_selection_reads_the_float();
    test_cutting_while_floated_fills_the_origin_only();
    test_a_fully_clipped_stroke_leaves_no_undo_entry();
    test_resize_undo_restores_the_selection();
    test_select_all_and_clear_selection();
    test_freehand_gesture_publishes_its_path();
    test_polygon_gesture_previews_and_escape_abandons_it();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
