// End-to-end UI tests: the real Slint window is driven with synthetic input
// events, and both the Slint-side state and the document behind it are
// asserted on. Runs head-less on Slint's testing backend.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "app/app_controller.hpp"
#include "appwindow.h"
#include "ui/ui_state.hpp"

#include "private/slint_tests_helpers.h"

namespace lsp {

using slint::language::PointerEventButton;

namespace {

int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(cond) check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

constexpr float kWindowWidth = 900;
constexpr float kWindowHeight = 600;

// Drag points that stay well inside the canvas below the tool bar, whatever
// its exact height. The marquee doubles as the reference for mapping between
// window and document coordinates.
constexpr float kMarqueeX0 = 150;
constexpr float kMarqueeY0 = 350;
constexpr float kMarqueeX1 = 350;
constexpr float kMarqueeY1 = 500;

// Inset by 20px on the top/left so the stroke stays inside the marquee even
// with the brush's round caps.
constexpr float kStrokeX0 = 170;
constexpr float kStrokeX1 = 330;
constexpr float kStrokeY = 370;

class Harness {
public:
    Harness() : m_window(AppWindow::create()), m_ui(std::make_unique<UiState>(*m_window, m_controller)) {
        m_window->show();
        m_window->window().dispatch_resize_event(slint::LogicalSize({kWindowWidth, kWindowHeight}));
        (void)m_window->window().take_snapshot();  // lay the scene out before feeding input
    }

    [[nodiscard]] const UserState& user() const { return m_window->global<UserState>(); }
    [[nodiscard]] const DocumentState& doc() const { return m_window->global<DocumentState>(); }
    [[nodiscard]] AppController& controller() { return m_controller; }

    void move(float x, float y) { m_window->window().dispatch_pointer_move_event(slint::LogicalPosition({x, y})); }

    void press(float x, float y, PointerEventButton button = PointerEventButton::Left) {
        move(x, y);
        m_window->window().dispatch_pointer_press_event(slint::LogicalPosition({x, y}), button);
    }

    void release(float x, float y, PointerEventButton button = PointerEventButton::Left) {
        m_window->window().dispatch_pointer_release_event(slint::LogicalPosition({x, y}), button);
    }

    void drag(float x0, float y0, float x1, float y1,
              PointerEventButton button = PointerEventButton::Left) {
        press(x0, y0, button);
        move((x0 + x1) / 2, (y0 + y1) / 2);
        move(x1, y1);
        release(x1, y1, button);
    }

    // A wheel notch at a window position. Positive delta scrolls up, which is
    // how the platform reports a notch towards the user.
    void scroll(float x, float y, float delta_y) {
        m_window->window().dispatch_pointer_scroll_event(slint::LogicalPosition({x, y}), 0, delta_y);
    }

    // Ctrl is tracked as window modifier state fed by key events, so the
    // control key's code has to be dispatched for the next event to carry it.
    void press_ctrl() { m_window->window().dispatch_key_press_event(slint::SharedString("\x11")); }

    void release_ctrl() { m_window->window().dispatch_key_release_event(slint::SharedString("\x11")); }

    void press_alt() { m_window->window().dispatch_key_press_event(slint::SharedString("\x12")); }

    void release_alt() { m_window->window().dispatch_key_release_event(slint::SharedString("\x12")); }

    [[nodiscard]] std::size_t popups() const {
        return slint::private_api::testing::active_popup_count(&*m_window);
    }

    // True when any pixel in the document is (approximately) the given colour.
    [[nodiscard]] bool any_pixel_is(unsigned char r, unsigned char g, unsigned char b,
                                    int within = 2) {
        const PixelBuffer& buffer = m_controller.document().flattened();
        for (int y = 0; y < buffer.height(); ++y) {
            for (int x = 0; x < buffer.width(); ++x) {
                const Rgba8& pixel = buffer.at(x, y);
                if (pixel.a > 200 && abs(int(pixel.r) - int(r)) <= within &&
                    abs(int(pixel.g) - int(g)) <= within && abs(int(pixel.b) - int(b)) <= within) {
                    return true;
                }
            }
        }
        return false;
    }

    // True when the document has a pixel close to (r, g, b) near (x, y).
    [[nodiscard]] bool color_near(int x, int y, unsigned char r, unsigned char g, unsigned char b,
                                  int radius = 4) {
        const PixelBuffer& buffer = m_controller.document().flattened();
        if (!buffer.contains(x, y)) return false;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int px = x + dx;
                const int py = y + dy;
                if (!buffer.contains(px, py)) continue;
                const Rgba8& pixel = buffer.at(px, py);
                if (pixel.a > 200 && abs(int(pixel.r) - int(r)) <= 2 && abs(int(pixel.g) - int(g)) <= 2 &&
                    abs(int(pixel.b) - int(b)) <= 2) {
                    return true;
                }
            }
        }
        return false;
    }

    // True when the document has a red pixel in a small neighbourhood of (x, y).
    [[nodiscard]] bool red_near(int x, int y, int radius = 4) {
        return color_near(x, y, 255, 0, 0, radius);
    }

private:
    slint::ComponentHandle<AppWindow> m_window;
    AppController m_controller;
    std::unique_ptr<UiState> m_ui;
};

// Window position -> document position, learned from the marquee's first corner.
struct Origin {
    float x = 0;
    float y = 0;
    [[nodiscard]] float to_window_x(float doc_x) const { return x + doc_x; }
    [[nodiscard]] float to_window_y(float doc_y) const { return y + doc_y; }
};

}  // namespace

int run_ui_tests() {
    slint::cbindgen_private::slint_testing_init_backend();

    Harness h;

    // 1. A fresh document has no selection and no clipboard content.
    CHECK(!h.doc().get_has_selection());
    CHECK(!h.doc().get_has_clipboard());
    CHECK(h.doc().get_canvas_width() == 800);
    CHECK(h.doc().get_canvas_height() == 600);

    // 2. The pencil draws. The primary colour is seeded fully transparent to
    //    catch alpha-0 ink: with the clamp in place the stroke must be red.
    h.user().set_primary_color(slint::Color::from_argb_uint8(0, 255, 0, 0));
    h.drag(kStrokeX0, kStrokeY, kStrokeX1, kStrokeY);
    CHECK(h.any_pixel_is(255, 0, 0));
    // ... and the colour written back to the UI must be opaque.
    CHECK(int(h.user().get_primary_color().alpha()) == 255);

    // 3. A left drag with the selection tool creates a marquee.
    h.user().set_selected_tool(ETool::SELECTION);
    h.drag(kMarqueeX0, kMarqueeY0, kMarqueeX1, kMarqueeY1);
    CHECK(h.doc().get_has_selection());
    const int sel_x = h.doc().get_selection_x();
    const int sel_y = h.doc().get_selection_y();
    CHECK(h.doc().get_selection_width() == int(kMarqueeX1 - kMarqueeX0));
    CHECK(h.doc().get_selection_height() == int(kMarqueeY1 - kMarqueeY0));

    Origin origin;
    origin.x = kMarqueeX0 - float(sel_x);
    origin.y = kMarqueeY0 - float(sel_y);

    // The stroke lies 20px below the marquee's top edge, so it is selected.
    const int stroke_y = sel_y + 20;
    CHECK(h.red_near(sel_x + 100, stroke_y));

    // 4. Dragging inside the selection moves its contents along with it. Color 2
    //    is green, so whatever the contents are dragged off of shows green —
    //    never a transparent hole in the canvas.
    h.user().set_secondary_color(slint::Color::from_rgb_uint8(0, 255, 0));
    const float inside_x = origin.to_window_x(sel_x + 50);
    const float inside_y = origin.to_window_y(sel_y + 50);
    h.drag(inside_x, inside_y, inside_x + 40, inside_y + 30);
    CHECK(h.doc().get_selection_x() == sel_x + 40);
    CHECK(h.doc().get_selection_y() == sel_y + 30);
    CHECK(h.doc().get_selection_width() == int(kMarqueeX1 - kMarqueeX0));
    CHECK(h.doc().get_selection_height() == int(kMarqueeY1 - kMarqueeY0));

    // The stroke travelled with the selection...
    CHECK(h.red_near(sel_x + 140, stroke_y + 30));
    // ... and its old place shows Color 2 instead of being left transparent.
    CHECK(!h.red_near(sel_x + 100, stroke_y));
    CHECK(h.color_near(sel_x + 100, stroke_y, 0, 255, 0));

    // 5. The move is undoable: bounds and contents are restored. The command
    //    bypasses the UI, so the flattened document is flushed by hand here.
    h.controller().history().undo();
    (void)h.controller().document().flush();
    const RectI restored = h.controller().document().selection().bounds();
    CHECK(restored.x == sel_x);
    CHECK(restored.y == sel_y);
    CHECK(h.red_near(sel_x + 100, stroke_y));
    CHECK(!h.color_near(sel_x + 100, stroke_y, 0, 255, 0));  // the fill goes with the move

    // 6. While another tool is active a right press must not open the menu.
    h.user().set_selected_tool(ETool::PENCIL);
    const float right_x = origin.to_window_x(sel_x + 10);
    const float right_y = origin.to_window_y(sel_y + 10);
    h.press(right_x, right_y, PointerEventButton::Right);
    CHECK(h.popups() == 0);
    h.release(right_x, right_y, PointerEventButton::Right);
    CHECK(!h.controller().input().active());

    // 7. With the selection tool active a right press opens the context menu,
    //    and the press must not reach the tool: a half-finished stroke would
    //    never see its button-up and would swallow the next click.
    h.user().set_selected_tool(ETool::SELECTION);
    h.press(right_x, right_y, PointerEventButton::Right);
    CHECK(h.popups() == 1);
    CHECK(!h.controller().input().active());

    // 8. The menu entries run the same callbacks the ribbon wires up. Rotating
    //    swaps the selection's dimensions (done twice to keep it in place for
    //    the steps below).
    h.user().invoke_transform_selection_content(EContentTransform::ROTATECW90);
    CHECK(h.doc().get_selection_width() == int(kMarqueeY1 - kMarqueeY0));
    CHECK(h.doc().get_selection_height() == int(kMarqueeX1 - kMarqueeX0));
    h.user().invoke_transform_selection_content(EContentTransform::ROTATECW90);
    CHECK(h.doc().get_selection_width() == int(kMarqueeX1 - kMarqueeX0));
    CHECK(h.doc().get_selection_height() == int(kMarqueeY1 - kMarqueeY0));

    // 9. Cut empties the selection but keeps it active, fills the clipboard
    //    (the ribbon enables Paste from it), and leaves Color 2 behind.
    h.user().invoke_cut_selection();
    CHECK(h.doc().get_has_selection());
    CHECK(h.doc().get_has_clipboard());
    CHECK(!h.any_pixel_is(255, 0, 0));
    CHECK(h.any_pixel_is(0, 255, 0));

    // 10. Paste puts the contents back as a new selection at the top left.
    h.user().invoke_paste_selection();
    CHECK(h.doc().get_has_selection());
    CHECK(h.doc().get_selection_x() == 0);
    CHECK(h.doc().get_selection_y() == 0);
    CHECK(h.doc().get_selection_width() == int(kMarqueeX1 - kMarqueeX0));
    CHECK(h.doc().get_selection_height() == int(kMarqueeY1 - kMarqueeY0));
    CHECK(h.any_pixel_is(255, 0, 0));

    // 11. Clicking away from the menu dismisses it (default close policy).
    h.press(60, 300);
    h.release(60, 300);
    CHECK(h.popups() == 0);

    // 12. Deselecting clears the flags the ribbon disables its buttons on.
    h.user().invoke_clear_selection();
    CHECK(!h.doc().get_has_selection());

    // 13. The marching-ants outline exists only while a selection does, and
    //     the light and dark runs together cover the border exactly once.
    CHECK(h.doc().get_ants_dark()->row_count() == 0);
    CHECK(h.doc().get_ants_light()->row_count() == 0);
    CHECK(h.doc().get_grips()->row_count() == 0);

    h.user().invoke_select_all();
    // The status bar's selection cell reads these: select-all spans the
    // whole paper.
    CHECK(h.doc().get_selection_width() == h.doc().get_canvas_width());
    CHECK(h.doc().get_selection_height() == h.doc().get_canvas_height());
    const auto dark = h.doc().get_ants_dark();
    const auto light = h.doc().get_ants_light();
    CHECK(dark->row_count() > 0);
    CHECK(light->row_count() > 0);
    int outline = 0;
    for (std::size_t i = 0; i < dark->row_count(); ++i)
        outline += int(dark->row_data(i)->w) * int(dark->row_data(i)->h);
    for (std::size_t i = 0; i < light->row_count(); ++i)
        outline += int(light->row_data(i)->w) * int(light->row_data(i)->h);
    const int sel_w = h.doc().get_selection_width();
    const int sel_h = h.doc().get_selection_height();
    CHECK(outline == 2 * (sel_w + sel_h - 2));

    // 14. A selection big enough shows all eight grips, centred on the
    //     selection's border.
    CHECK(h.doc().get_grips()->row_count() == 8);
    const auto first_grip = h.doc().get_grips()->row_data(0);
    CHECK(first_grip.has_value());
    CHECK(first_grip->x == float(h.doc().get_selection_x()));
    CHECK(first_grip->y == float(h.doc().get_selection_y()));

    // 15. The grips belong to the selection tool: switching away takes them
    //     while the outline stays for any tool to see.
    h.user().set_selected_tool(ETool::PENCIL);
    h.user().invoke_select_all();  // any callback re-syncs the document state
    CHECK(h.doc().get_grips()->row_count() == 0);
    CHECK(h.doc().get_ants_dark()->row_count() > 0);

    h.user().invoke_clear_selection();
    CHECK(h.doc().get_ants_dark()->row_count() == 0);
    CHECK(h.doc().get_ants_light()->row_count() == 0);
    CHECK(h.doc().get_grips()->row_count() == 0);

    // 15b. The status bar's pointer cell: at zoom 1 with the view unrolled
    //      the window point maps straight onto the document, and over the
    //      gray around the paper the readout blanks instead of going stale.
    h.move(origin.x + 100, origin.y + 100);
    CHECK(h.user().get_cursor_on_canvas());
    CHECK(std::abs(h.user().get_cursor_x() - 100) < 1.0);
    CHECK(std::abs(h.user().get_cursor_y() - 100) < 1.0);

    h.move(origin.x - 2, origin.y - 2);
    CHECK(!h.user().get_cursor_on_canvas());

    // 16. Ctrl+wheel over the gray outside the paper zooms with the transform
    //     origin at the canvas' top-left: the viewport does not slide, so the
    //     window point maps through the new zoom from the same origin. A
    //     cursor-anchored zoom would have shifted it by -75.
    h.user().set_primary_color(slint::Color::from_rgb_uint8(255, 0, 0));
    h.move(origin.x + 300, origin.y - 2);  // the margin just above the paper
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y - 2, 60);
    h.release_ctrl();
    CHECK(h.user().get_zoom() == 1.25);

    h.drag(origin.x + 300, origin.y + 200, origin.x + 350, origin.y + 200);
    CHECK(h.color_near(240, 160, 255, 0, 0));   // (300, 200) view px / 1.25
    CHECK(h.color_near(280, 160, 255, 0, 0));   // 50 screen px are 40 doc px
    CHECK(!h.color_near(300, 160, 255, 0, 0));  // anchored at the cursor: 300

    // 17. Ctrl+wheel over the paper zooms around the cursor instead: the
    //     point under the wheel keeps its document pixel through a second
    //     notch as well, raw view coordinates would wander, and the plain
    //     wheel stays a pan.
    h.move(origin.x + 300, origin.y + 300);
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 60);
    h.release_ctrl();
    CHECK(h.user().get_zoom() == 1.5625);

    h.drag(origin.x + 300, origin.y + 300, origin.x + 350, origin.y + 300);
    CHECK(h.color_near(240, 240, 255, 0, 0));   // held through the second zoom
    CHECK(h.color_near(272, 240, 255, 0, 0));   // 50 screen px are 32 doc px
    CHECK(!h.color_near(192, 192, 255, 0, 0));  // without the anchor: 192
    CHECK(!h.color_near(307, 297, 255, 0, 0));  // raw moves would slope here

    h.scroll(origin.x + 300, origin.y + 300, 60);
    CHECK(h.user().get_zoom() == 1.5625);  // plain wheel only pans

    // 18. The magnify tool zooms from the canvas itself: a left press steps
    //     in, a right or alt press steps out, anchored at the click point.
    //     The press must not reach the document tool pipeline (no tool is
    //     registered for magnify), and a pencil drag right after must still
    //     draw: a dangling pointer-down would swallow it.
    h.user().set_selected_tool(ETool::MAGNIFY);
    h.press(origin.x + 300, origin.y + 300);
    h.release(origin.x + 300, origin.y + 300);
    CHECK(std::abs(h.user().get_zoom() - 1.953125) < 0.0001);  // 1.5625 * 1.25

    h.press(origin.x + 300, origin.y + 300, PointerEventButton::Right);
    h.release(origin.x + 300, origin.y + 300, PointerEventButton::Right);
    CHECK(std::abs(h.user().get_zoom() - 1.5625) < 0.0001);  // right steps out

    h.press_alt();
    h.press(origin.x + 300, origin.y + 300);
    h.release(origin.x + 300, origin.y + 300);
    h.release_alt();
    CHECK(std::abs(h.user().get_zoom() - 1.25) < 0.0001);  // alt steps out too

    // Back to the pencil: the next drag has to land in the document.
    h.user().set_selected_tool(ETool::PENCIL);
    h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 255, 0));
    h.drag(origin.x + 300, origin.y + 300, origin.x + 350, origin.y + 300);
    CHECK(h.any_pixel_is(0, 255, 0));

    // 19. Ctrl+wheel counts in 60px notches: half a notch banks up instead
    //     of zooming, the second half lands exactly one step, and a plain
    //     wheel afterwards still only pans. The position is the one step 17
    //     panned at on purpose: the ScrollView's Flickable arms an 800ms
    //     scroll filter whenever it pans itself, and that filter used to
    //     swallow every wheel near the same spot afterwards — the ctrl
    //     zoom going dead right after a plain scroll.
    const float z_bank = h.user().get_zoom();
    h.move(origin.x + 300, origin.y + 300);
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 30);
    CHECK(std::abs(h.user().get_zoom() - z_bank) < 0.0001);  // banked, not stepped

    h.scroll(origin.x + 300, origin.y + 300, 30);
    CHECK(std::abs(h.user().get_zoom() - z_bank * 1.25) < 0.0001);  // one step

    h.release_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 60);
    CHECK(std::abs(h.user().get_zoom() - z_bank * 1.25) < 0.0001);  // pan only

    if (g_failures != 0) {
        std::fprintf(stderr, "%d UI check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("UI OK\n");
    return 0;
}

}  // namespace lsp

int main() { return lsp::run_ui_tests(); }
