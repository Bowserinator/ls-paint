// End-to-end UI tests: the real Slint window is driven with synthetic input
// events, and both the Slint-side state and the document behind it are
// asserted on. Runs head-less on Slint's testing backend.

#include <cmath>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "app/app_controller.hpp"
#include "appwindow.h"
#include "config.hpp"
#include "confirm-dialog.h"
#include "edit-colors-window.h"
#include "io/document_io.hpp"
#include "store/store.hpp"
#include "store/store_sqlite.hpp"
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
        // The window opens with the rulers on. The checks below measure the view
        // itself, so they start from the paper's own corner.
        m_window->global<UserState>().set_show_rulers(false);
        m_window->window().dispatch_resize_event(slint::LogicalSize({kWindowWidth, kWindowHeight}));
        (void)m_window->window().take_snapshot();  // lay the scene out before feeding input
    }

    [[nodiscard]] const UserState& user() const { return m_window->global<UserState>(); }
    [[nodiscard]] slint::Window& window() { return m_window->window(); }
    [[nodiscard]] const DocumentState& doc() const { return m_window->global<DocumentState>(); }
    [[nodiscard]] lsp_message::MessageDialog& message() const { return m_ui->message_window(); }

    [[nodiscard]] bool message_open() const { return m_ui->message_window_open(); }

    [[nodiscard]] lsp_resize::ResizeDialog& resize() const { return m_ui->resize_window(); }

    [[nodiscard]] bool resize_open() const { return m_ui->resize_window_open(); }
    [[nodiscard]] const ToolOptions& tool_options() const { return m_window->global<ToolOptions>(); }
    [[nodiscard]] AppController& controller() { return m_controller; }

    [[nodiscard]] const ShapeStyle& shapes() const { return m_window->global<ShapeStyle>(); }

    // The outline and the fill are chosen separately now, so a test that wants a
    // combination has to set both parts.
    void set_shape_textures(EShapeTexture outline, EShapeTexture fill) {
        m_window->global<ShapeStyle>().set_outline(outline);
        m_window->global<ShapeStyle>().set_fill(fill);
    }

    [[nodiscard]] const ShapeOverlay& overlay() const { return m_window->global<ShapeOverlay>(); }

    // The Edit Colors surface is a window of its own, so the suite drives it
    // through the UI state rather than through a global.
    [[nodiscard]] lsp_colors::EditColorsWindow& picker() const { return m_ui->color_window(); }

    [[nodiscard]] bool picker_open() const { return m_ui->color_window_open(); }

    // The unsaved-changes question is a window of its own as well.
    [[nodiscard]] lsp_confirm::ConfirmDialog& confirm() const { return m_ui->confirm_window(); }

    [[nodiscard]] bool confirm_open() const { return m_ui->confirm_window_open(); }

    [[nodiscard]] const TextOverlay& text_box() const { return m_window->global<TextOverlay>(); }

    [[nodiscard]] const FontStyle& font_style() const { return m_window->global<FontStyle>(); }
    [[nodiscard]] const RotateOverlay& rotate_overlay() const
    {
        return m_window->global<RotateOverlay>();
    }

    [[nodiscard]] std::size_t gallery_size() const {
        return m_window->global<ShapeGallery>().get_items()->row_count();
    }

    void move(float x, float y) {
        CHECK(in_window(x, y));
        m_window->window().dispatch_pointer_move_event(slint::LogicalPosition({x, y}));
    }

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

    // One press and release of a key, named by the text the platform reports
    // for it: a character, "\n" for Enter and "\u{1b}" for Escape.
    void key(const std::string& code) {
        m_window->window().dispatch_key_press_event(slint::SharedString(code));
        m_window->window().dispatch_key_release_event(slint::SharedString(code));
    }

    void type(const std::string& text) {
        for (const char letter : text) key(std::string(1, letter));
    }

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
                if (pixel.a > 200 && std::abs(int(pixel.r) - int(r)) <= within &&
                    std::abs(int(pixel.g) - int(g)) <= within && std::abs(int(pixel.b) - int(b)) <= within) {
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
                if (pixel.a > 200 && std::abs(int(pixel.r) - int(r)) <= 2 && std::abs(int(pixel.g) - int(g)) <= 2 &&
                    std::abs(int(pixel.b) - int(b)) <= 2) {
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

    // How many document pixels near the given colour a rectangle holds.
    // Ink is anti-aliased, so a glyph has few or no pixels of the pure ink
    // colour. This counts darkened pixels instead, whatever shade they took.
    [[nodiscard]] int count_ink(int x0, int y0, int x1, int y1) {
        const PixelBuffer& buffer = m_controller.document().flattened();
        int count = 0;
        for (int y = std::max(0, y0); y <= std::min(buffer.height() - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(buffer.width() - 1, x1); ++x) {
                const Rgba8& pixel = buffer.at(x, y);
                if (pixel.a > 200 && (pixel.r < 250 || pixel.g < 250 || pixel.b < 250)) ++count;
            }
        }
        return count;
    }

    [[nodiscard]] int count_color(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g,
                                  unsigned char b) {
        const PixelBuffer& buffer = m_controller.document().flattened();
        int count = 0;
        for (int y = std::max(0, y0); y <= std::min(buffer.height() - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(buffer.width() - 1, x1); ++x) {
                const Rgba8& pixel = buffer.at(x, y);
                if (pixel.a > 200 && std::abs(int(pixel.r) - int(r)) <= 2 &&
                    std::abs(int(pixel.g) - int(g)) <= 2 && std::abs(int(pixel.b) - int(b)) <= 2)
                    ++count;
            }
        }
        return count;
    }

private:
    // An event outside the window is dropped, which would leave a tool mid
    // drag and look like a broken tool rather than a bad test coordinate.
    [[nodiscard]] static bool in_window(float x, float y) {
        return x >= 0 && y >= 0 && x <= kWindowWidth && y <= kWindowHeight;
    }

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

    // The settings store is a real database, so every window made here writes
    // to a directory of the tests' own rather than to the user's settings.
    const std::filesystem::path config_dir =
        std::filesystem::temp_directory_path() / "lsp_ui_config";
    std::error_code config_error;
    std::filesystem::remove_all(config_dir, config_error);
    std::filesystem::create_directories(config_dir, config_error);
#if defined(_WIN32)
    _putenv_s("APPDATA", config_dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", config_dir.string().c_str(), 1);
#endif

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

    // 14b. Escape is Deselect: the outline and the grips stop, and the
    //      ribbon's selection buttons go back to being disabled.
    h.key("\x1b");
    CHECK(!h.doc().get_has_selection());
    CHECK(h.doc().get_ants_dark()->row_count() == 0);
    CHECK(h.doc().get_ants_light()->row_count() == 0);
    CHECK(h.doc().get_grips()->row_count() == 0);

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

    const Rgba8 under = h.controller().document().flattened().at(100, 100);
    char expected_hex[8];
    std::snprintf(expected_hex, sizeof(expected_hex), "#%02X%02X%02X", under.r, under.g, under.b);
    CHECK(std::string(h.user().get_hover_color_text()) == "(" + std::to_string(under.r) + "," +
                                                              std::to_string(under.g) + "," +
                                                              std::to_string(under.b) + ") " +
                                                              expected_hex);

    h.move(origin.x - 2, origin.y - 2);
    CHECK(!h.user().get_cursor_on_canvas());
    h.user().invoke_select_all();  // any callback re-syncs the view state
    CHECK(std::string(h.user().get_hover_color_text()).empty());
    h.user().invoke_clear_selection();

    // 15b-2. The View tab's Status Bar box is two-way bound to this property,
    //       which is what the window instantiates the strip from: clearing it
    //       is what gives its space back, and the document is untouched.
    const PixelBuffer strip_before = h.controller().document().flattened();
    CHECK(h.user().get_show_status_bar());
    h.user().set_show_status_bar(false);
    CHECK(!h.user().get_show_status_bar());
    h.move(origin.x + 100, origin.y + 100);
    CHECK(h.controller().document().flattened() == strip_before);
    h.user().set_show_status_bar(true);
    CHECK(h.user().get_show_status_bar());

    // 15b-3. The View tab's Gridlines box only changes what the window draws
    //       over the paper: the document itself is untouched.
    const PixelBuffer paper_before = h.controller().document().flattened();
    h.user().set_show_gridlines(true);
    h.user().invoke_select_all();  // any callback re-syncs the view state
    h.user().invoke_clear_selection();
    CHECK(h.controller().document().flattened() == paper_before);
    h.user().set_show_gridlines(false);

    // 15b-4. So is the Rulers box: the strips are window furniture, holding the
    //       view clear while they show, so turning it off gives the space back
    //       and the paper sits where it did.
    h.user().set_show_rulers(true);
    (void)h.window().take_snapshot();
    h.move(origin.x + 100, origin.y + 100);
    CHECK(h.user().get_cursor_on_canvas());
    // The strips are 18px thick (Styles.ruler-size), so a fixed point on the
    // screen covers a place 20 document pixels further up and left.
    CHECK(std::abs(h.user().get_cursor_x() - 82) < 1.0);
    CHECK(std::abs(h.user().get_cursor_y() - 82) < 1.0);
    CHECK(h.controller().document().flattened() == paper_before);
    h.user().set_show_rulers(false);
    (void)h.window().take_snapshot();
    h.move(origin.x + 100, origin.y + 100);
    CHECK(std::abs(h.user().get_cursor_x() - 100) < 1.0);

    // 15b-5. The rotate ring: with the Selection tool active, a pointer in the
    //       ring just outside a corner raises the badge, and a press there turns
    //       the contents.
    h.user().set_selected_tool(ETool::SELECTION);
    h.press(origin.x + 100, origin.y + 100);
    h.move(origin.x + 160, origin.y + 160);
    h.release(origin.x + 160, origin.y + 160);
    CHECK(h.doc().get_has_selection());

    // Over the contents there is no ring, only further out of the corner.
    h.move(origin.x + 130, origin.y + 130);
    CHECK(!h.rotate_overlay().get_hover());
    // Just outside the north-west corner, within the tool's 12px reach.
    h.move(origin.x + 92, origin.y + 92);
    CHECK(h.rotate_overlay().get_hover());
    CHECK(!h.rotate_overlay().get_rotating());

    // A drag from the ring publishes the turn while it runs, and clears it when
    // the drag ends.
    h.press(origin.x + 92, origin.y + 92);
    CHECK(h.rotate_overlay().get_rotating());
    h.move(origin.x + 140, origin.y + 92);
    CHECK(h.rotate_overlay().get_rotating());
    h.release(origin.x + 140, origin.y + 92);
    CHECK(!h.rotate_overlay().get_rotating());
    CHECK(!h.rotate_overlay().get_hover());

    // The outline, the grips and the status bar's size all read the mask, which
    // is now the bounding box of the turned contents.
    const RectI turned_box = h.controller().document().selection().bounds();
    CHECK(turned_box.width > 60 || turned_box.height > 60);  // the turn grew the box
    CHECK(h.doc().get_selection_width() == turned_box.width);
    CHECK(h.doc().get_selection_height() == turned_box.height);
    CHECK(h.doc().get_grips()->row_count() == 8);
    CHECK(h.doc().get_ants_dark()->row_count() > 0);

    // Take the turn back and drop the selection: the shape checks below draw
    // outside it, and a live selection clips what the tools can paint.
    h.controller().history().undo();
    h.user().invoke_clear_selection();
    (void)h.controller().document().flush();
    CHECK(!h.doc().get_has_selection());

    // 15c. The Shapes tool: a press shows a live overlay that never touches the
    //      raster, and the release commits the whole shape as one entry, so a
    //      single undo takes it back to blank paper.
    h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 0, 255));
    h.user().set_secondary_color(slint::Color::from_rgb_uint8(255, 255, 0));
    h.shapes().set_kind(EShapeKind::OVAL);
    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::NONE);
    h.shapes().set_thickness(1);
    h.user().set_selected_tool(ETool::SHAPE);

    h.press(origin.x + 420, origin.y + 60);
    h.move(origin.x + 470, origin.y + 110);
    CHECK(h.controller().tool_ui().shape.visible);
    CHECK(!h.controller().tool_ui().shape.commands.empty());
    CHECK(h.overlay().get_visible());
    CHECK(h.count_color(418, 58, 472, 112, 0, 0, 255) == 0);
    h.release(origin.x + 470, origin.y + 110);
    CHECK(!h.overlay().get_visible());
    CHECK(h.count_color(418, 58, 472, 112, 0, 0, 255) > 0);
    CHECK(!h.color_near(445, 85, 0, 0, 255));  // an outline leaves the middle empty

    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.count_color(418, 58, 472, 112, 0, 0, 255) == 0);

    //      Every kind the picker offers draws inside its own drag box, and one
    //      undo takes each of them back. The polygon has its own step below:
    //      it closes on a click instead of on the button release.
    const EShapeKind kinds[] = {
        EShapeKind::LINE,           EShapeKind::CURVE,          EShapeKind::OVAL,
        EShapeKind::RECTANGLE,      EShapeKind::ROUNDEDRECTANGLE, EShapeKind::TRIANGLE,
        EShapeKind::RIGHTTRIANGLE,  EShapeKind::DIAMOND,        EShapeKind::PENTAGON,
        EShapeKind::HEXAGON,        EShapeKind::ARROWRIGHT,     EShapeKind::ARROWLEFT,
        EShapeKind::ARROWUP,        EShapeKind::ARROWDOWN,      EShapeKind::STAR4,
        EShapeKind::STAR5,          EShapeKind::STAR6,          EShapeKind::CALLOUTROUNDED,
        EShapeKind::CALLOUTOVAL,    EShapeKind::CALLOUTCLOUD,   EShapeKind::HEART,
        EShapeKind::LIGHTNING};
    constexpr int kKindColumns = 5;
    for (int i = 0; i < int(sizeof(kinds) / sizeof(kinds[0])); ++i) {
        const int x0 = 420 + (i % kKindColumns) * 70;
        const int y0 = 60 + (i / kKindColumns) * 70;
        h.shapes().set_kind(kinds[i]);
        h.drag(origin.x + float(x0), origin.y + float(y0), origin.x + float(x0 + 50),
               origin.y + float(y0 + 50));
        CHECK(h.count_color(x0 - 2, y0 - 2, x0 + 52, y0 + 52, 0, 0, 255) > 0);
        h.controller().history().undo();
        (void)h.controller().document().flush();
        CHECK(h.count_color(x0 - 2, y0 - 2, x0 + 52, y0 + 52, 0, 0, 255) == 0);
    }

    //      The same kinds filled: every closed kind covers more ground than its
    //      own outline does, and the interior takes the second colour. A kind
    //      whose fill left a gap behind would cover fewer pixels than the
    //      outline that drew its edge.
    int filled_kinds = 0;
    for (int i = 0; i < int(sizeof(kinds) / sizeof(kinds[0])); ++i) {
        if (kinds[i] == EShapeKind::LINE || kinds[i] == EShapeKind::CURVE) continue;
        const int x0 = 420 + (i % kKindColumns) * 70;
        const int y0 = 60 + (i / kKindColumns) * 70;
        h.shapes().set_kind(kinds[i]);
        h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::NONE);
        h.drag(origin.x + float(x0), origin.y + float(y0), origin.x + float(x0 + 50),
               origin.y + float(y0 + 50));
        const int outline_ink = h.count_color(x0 - 2, y0 - 2, x0 + 52, y0 + 52, 0, 0, 255);
        h.controller().history().undo();
        (void)h.controller().document().flush();

        h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::SOLID);
        h.drag(origin.x + float(x0), origin.y + float(y0), origin.x + float(x0 + 50),
               origin.y + float(y0 + 50));
        CHECK(h.count_color(x0 - 2, y0 - 2, x0 + 52, y0 + 52, 255, 255, 0) > outline_ink);
        h.controller().history().undo();
        (void)h.controller().document().flush();
        CHECK(h.count_color(x0 - 2, y0 - 2, x0 + 52, y0 + 52, 255, 255, 0) == 0);
        ++filled_kinds;
    }
    CHECK(filled_kinds == 20);

    // 15d. Outline, fill and both: the fill reaches the middle in the second
    //      colour, and a thicker outline covers more pixels than a thin one.
    h.shapes().set_kind(EShapeKind::OVAL);
    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::NONE);
    h.shapes().set_thickness(1);
    h.drag(origin.x + 420, origin.y + 60, origin.x + 470, origin.y + 110);
    const int thin_outline = h.count_color(418, 58, 472, 112, 0, 0, 255);

    h.shapes().set_thickness(6);
    h.drag(origin.x + 560, origin.y + 60, origin.x + 610, origin.y + 110);
    CHECK(h.count_color(558, 58, 612, 112, 0, 0, 255) > thin_outline);

    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::SOLID);
    h.shapes().set_thickness(1);
    h.drag(origin.x + 630, origin.y + 60, origin.x + 680, origin.y + 110);
    CHECK(h.color_near(655, 85, 255, 255, 0));  // the fill takes Color 2, and
    CHECK(h.count_color(628, 58, 682, 112, 0, 0, 255) > 0);  // the border is drawn too

    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::SOLID);
    h.drag(origin.x + 700, origin.y + 60, origin.x + 750, origin.y + 110);
    CHECK(h.color_near(725, 85, 255, 255, 0));  // the interior takes Color 2
    CHECK(h.count_color(698, 58, 752, 112, 0, 0, 255) > 0);

    // 15e. The polygon builds up click by click, and a click back on its first
    //      vertex closes it into one committed shape.
    h.shapes().set_kind(EShapeKind::POLYGON);
    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::NONE);
    h.shapes().set_thickness(1);
    h.press(origin.x + 420, origin.y + 200);
    h.release(origin.x + 420, origin.y + 200);
    h.press(origin.x + 520, origin.y + 200);
    h.release(origin.x + 520, origin.y + 200);
    h.press(origin.x + 470, origin.y + 290);
    h.release(origin.x + 470, origin.y + 290);
    CHECK(h.count_color(418, 198, 522, 292, 0, 0, 255) == 0);  // still open
    h.press(origin.x + 424, origin.y + 203);  // inside the closing radius
    h.release(origin.x + 424, origin.y + 203);
    CHECK(!h.controller().tool_ui().shape.visible);
    CHECK(h.count_color(418, 198, 522, 292, 0, 0, 255) > 0);
    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.count_color(418, 198, 522, 292, 0, 0, 255) == 0);

    // 15f. An active selection clips the shape: the drag reaches past it, but
    //      nothing outside the selection is written. The tool is left as the
    //      pencil, which is what the view steps below expect.
    h.shapes().set_kind(EShapeKind::RECTANGLE);
    h.set_shape_textures(EShapeTexture::SOLID, EShapeTexture::NONE);
    h.shapes().set_thickness(1);
    h.user().set_selected_tool(ETool::SELECTION);
    h.drag(origin.x + 400, origin.y + 300, origin.x + 500, origin.y + 350);
    CHECK(h.doc().get_has_selection());
    h.user().set_selected_tool(ETool::SHAPE);
    h.drag(origin.x + 400, origin.y + 300, origin.x + 560, origin.y + 350);
    CHECK(h.count_color(402, 296, 498, 354, 0, 0, 255) > 0);
    CHECK(h.count_color(505, 296, 565, 354, 0, 0, 255) == 0);
    h.user().invoke_clear_selection();
    CHECK(!h.doc().get_has_selection());
    h.user().set_selected_tool(ETool::PENCIL);

    // 15g. The picker carries one glyph per kind, built from the geometry the
    //      tool draws with.
    CHECK(h.gallery_size() == 23);

    // 15h. The text tool: a click opens a box that takes focus, and typing
    //      reaches the box only. Nothing is painted until the box closes.
    h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 0, 255));
    h.user().set_selected_tool(ETool::TEXT);
    CHECK(h.count_ink(58, 58, 240, 84) == 0);  // the band starts blank
    h.press(origin.x + 60, origin.y + 60);
    h.release(origin.x + 60, origin.y + 60);
    CHECK(h.text_box().get_visible());
    h.type("Hello");
    CHECK(h.text_box().get_content() == "Hello");
    CHECK(h.count_ink(58, 58, 240, 84) == 0);
    h.key("\x1b");  // Escape abandons the box and draws nothing
    CHECK(!h.text_box().get_visible());
    CHECK(h.count_ink(58, 58, 240, 84) == 0);

    //      A box closed by a click elsewhere does draw, and one undo takes the
    //      whole box back.
    h.press(origin.x + 60, origin.y + 60);
    h.release(origin.x + 60, origin.y + 60);
    h.type("Hello");
    h.press(origin.x + 420, origin.y + 60);
    h.release(origin.x + 420, origin.y + 60);
    CHECK(h.count_ink(58, 58, 240, 84) > 0);
    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.count_ink(58, 58, 240, 84) == 0);
    // A box opened by the click that dismissed the last one does not take the
    // keyboard, so the tool switch is what closes it here.
    h.user().set_selected_tool(ETool::PENCIL);
    h.press(origin.x + 700, origin.y + 350);
    h.release(origin.x + 700, origin.y + 350);
    CHECK(!h.text_box().get_visible());
    h.user().set_selected_tool(ETool::TEXT);

    // 15i. Enter starts a second line, so one box can hold several lines.
    h.press(origin.x + 60, origin.y + 120);
    h.release(origin.x + 60, origin.y + 120);
    h.type("A");
    h.key("\n");
    h.type("B");
    CHECK(h.text_box().get_content() == "A\nB");
    h.press(origin.x + 420, origin.y + 120);
    h.release(origin.x + 420, origin.y + 120);
    CHECK(h.count_ink(58, 118, 140, 140) > 0);
    CHECK(h.count_ink(58, 140, 140, 166) > 0);
    h.user().set_selected_tool(ETool::PENCIL);
    h.press(origin.x + 700, origin.y + 350);
    h.release(origin.x + 700, origin.y + 350);
    CHECK(!h.text_box().get_visible());
    h.user().set_selected_tool(ETool::TEXT);

    // 15j. A click elsewhere commits the box, and the press that dismissed it
    //      opens the next one. Leaving the tool with an empty box writes nothing.
    h.press(origin.x + 40, origin.y + 250);
    h.release(origin.x + 40, origin.y + 250);
    CHECK(h.text_box().get_visible());
    h.type("Hi");
    CHECK(h.text_box().get_content() == "Hi");
    h.press(origin.x + 450, origin.y + 250);
    h.release(origin.x + 450, origin.y + 250);
    CHECK(h.count_ink(38, 248, 130, 274) > 0);  // "Hi" landed where it was typed
    CHECK(h.text_box().get_visible());          // and a new box is open
    h.user().set_selected_tool(ETool::PENCIL);
    h.press(origin.x + 700, origin.y + 350);  // any canvas event runs the tool switch
    h.release(origin.x + 700, origin.y + 350);
    CHECK(!h.text_box().get_visible());
    CHECK(h.count_ink(448, 248, 560, 280) == 0);  // the empty box painted nothing

    // 15k. A texture is previewed in its base colour, and a shape with neither
    //      an outline nor a fill paints nothing and takes no undo step.
    h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 0, 255));
    h.user().set_secondary_color(slint::Color::from_rgb_uint8(255, 255, 0));
    h.shapes().set_kind(EShapeKind::RECTANGLE);
    h.shapes().set_thickness(1);
    h.user().set_selected_tool(ETool::SHAPE);
    h.set_shape_textures(EShapeTexture::CRAYON, EShapeTexture::WATERCOLOR);
    h.press(origin.x + 420, origin.y + 380);
    h.move(origin.x + 500, origin.y + 440);
    CHECK(h.overlay().get_visible());
    CHECK(h.overlay().get_draw_outline());
    CHECK(h.overlay().get_draw_fill());
    const slint::Color preview_outline = h.overlay().get_outline_color();
    const slint::Color preview_fill = h.overlay().get_fill_color();
    CHECK(preview_outline.red() == 0 && preview_outline.green() == 0 &&
          preview_outline.blue() == 255);
    CHECK(preview_fill.red() == 255 && preview_fill.green() == 255 && preview_fill.blue() == 0);
    CHECK(h.count_ink(418, 378, 502, 442) == 0);  // the preview never touches the paper
    h.release(origin.x + 500, origin.y + 440);
    CHECK(!h.overlay().get_visible());
    CHECK(h.count_ink(418, 378, 502, 442) > 0);  // the crayon edge landed
    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.count_ink(418, 378, 502, 442) == 0);

    h.set_shape_textures(EShapeTexture::NONE, EShapeTexture::NONE);
    h.press(origin.x + 560, origin.y + 380);
    h.move(origin.x + 640, origin.y + 440);
    CHECK(!h.overlay().get_draw_outline());
    CHECK(!h.overlay().get_draw_fill());
    h.release(origin.x + 640, origin.y + 440);
    CHECK(h.count_ink(558, 378, 642, 442) == 0);
    h.user().set_selected_tool(ETool::PENCIL);

    // 16. Ctrl+wheel over the gray outside the paper zooms around the cursor
    //     like it does over the paper, so the view slides by the anchor and the
    //     stroke drawn after it lands where the slid window point maps through
    //     the new zoom.
    h.user().set_primary_color(slint::Color::from_rgb_uint8(255, 0, 0));
    h.move(origin.x + 300, origin.y - 2);  // the margin just above the paper
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y - 2, 60);
    h.release_ctrl();
    CHECK(h.user().get_zoom() == 1.25);

    h.drag(origin.x + 300, origin.y + 200, origin.x + 350, origin.y + 200);
    CHECK(h.color_near(300, 160, 255, 0, 0));   // (300, 200) slid by -75, / 1.25
    CHECK(h.color_near(340, 160, 255, 0, 0));   // 50 screen px are 40 doc px
    CHECK(!h.color_near(240, 160, 255, 0, 0));  // what an unslid view would give

    // 17. Ctrl+wheel over the paper holds the point under the wheel through a
    //     second notch as well, raw view coordinates would wander, and the
    //     plain wheel stays a pan.
    h.move(origin.x + 300, origin.y + 300);
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 60);
    h.release_ctrl();
    CHECK(h.user().get_zoom() == 1.5625);

    h.drag(origin.x + 300, origin.y + 300, origin.x + 350, origin.y + 300);
    CHECK(h.color_near(300, 240, 255, 0, 0));   // held through the second zoom
    CHECK(h.color_near(310, 240, 255, 0, 0));   // and the stroke runs on from it
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
    const float z_start = h.user().get_zoom();
    h.move(origin.x + 300, origin.y + 300);
    h.press_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 30);
    // Half a notch is half the travel, so it is pow(1.25, 0.5) of the view:
    // neither a whole step nor nothing.
    CHECK(std::abs(h.user().get_zoom() - z_start * std::pow(1.25f, 0.5f)) < 0.001);

    h.scroll(origin.x + 300, origin.y + 300, 30);
    CHECK(std::abs(h.user().get_zoom() - z_start * 1.25) < 0.001);  // two halves

    h.release_ctrl();
    h.scroll(origin.x + 300, origin.y + 300, 60);
    CHECK(std::abs(h.user().get_zoom() - z_start * 1.25) < 0.001);  // pan only

    // 20. A drawing carries the unsaved marker, and the title names the paper.
    CHECK(h.doc().get_modified());
    CHECK(h.doc().get_title() == "untitled");

    // 21. The file and paste pickers are the system's own dialogs now, so they
    //     cannot be driven from a test. What the window owns is what a save and
    //     an open do to the document, and that is checked below through the
    //     same controller calls the dialogs feed.

    // 22. Ctrl+N over unsaved work asks first: cancelling keeps the paper.
    h.press_ctrl();
    h.key("n");
    h.release_ctrl();
    CHECK(h.confirm_open());
    CHECK(!h.confirm().get_message().empty());
    h.confirm().invoke_cancelled();
    CHECK(!h.confirm_open());
    CHECK(h.doc().get_modified());

    // 23. Saving names the document, clears the marker, and the file it writes
    //     opens again. The mark is drawn in a corner no earlier step touched.
    h.user().set_selected_tool(ETool::PENCIL);
    h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 0, 255));
    h.tool_options().set_brush_size(8);
    h.drag(origin.x + 600, origin.y + 60, origin.x + 700, origin.y + 60);
    CHECK(h.count_color(600, 54, 700, 66, 0, 0, 255) > 0);

    const std::filesystem::path save_path =
        std::filesystem::temp_directory_path() / "ls_paint_ui_round_trip.png";
    std::filesystem::remove(save_path);
    CHECK(h.controller().save_document(save_path, ImageFormat::Png).ok);
    CHECK(std::filesystem::exists(save_path));
    h.move(origin.x + 300, origin.y + 300);  // let the window catch up
    CHECK(h.doc().get_title() == "ls_paint_ui_round_trip.png");
    CHECK(!h.doc().get_modified());

    // 24. Ctrl+N over a saved document asks nothing and empties the paper.
    h.press_ctrl();
    h.key("n");
    h.release_ctrl();
    CHECK(!h.confirm_open());
    CHECK(h.doc().get_title() == "untitled");
    CHECK(!h.doc().get_modified());
    CHECK(h.count_color(600, 54, 700, 66, 0, 0, 255) == 0);

    // 25. Opening the saved file brings the mark back.
    CHECK(h.controller().open_document(save_path).ok);
    h.move(origin.x + 300, origin.y + 300);
    CHECK(h.count_color(600, 54, 700, 66, 0, 0, 255) > 0);
    CHECK(h.doc().get_title() == "ls_paint_ui_round_trip.png");
    CHECK(!h.doc().get_modified());

    // 26. A file that is not there is refused, and the paper is left as it was.
    const IoResult missing = h.controller().open_document("/nonexistent/ls_paint_none.png");
    CHECK(!missing.ok);
    CHECK(!missing.error.empty());
    h.move(origin.x + 300, origin.y + 300);
    CHECK(h.doc().get_title() == "ls_paint_ui_round_trip.png");

    // 27. Paste from file adds the picture to the paper as an edit, and hands
    //     it to the selection tool the way the window does.
    CHECK(h.controller().paste_from_file(save_path).ok);
    h.user().set_selected_tool(ETool::SELECTION);
    h.move(origin.x + 300, origin.y + 300);
    CHECK(h.doc().get_modified());

    // 29. A brush preset reaches the next stroke: the square tip covers the
    //     corners of its box, the dotted tip leaves gaps. This runs before the
    //     resize below, which moves the view and so invalidates the window to
    //     document mapping this harness learned from the marquee.
    h.user().set_selected_tool(ETool::PENCIL);
    h.user().set_primary_color(slint::Color::from_rgb_uint8(255, 0, 0));
    h.tool_options().set_brush_size(8);
    h.tool_options().set_brush_spacing(0.25);
    h.tool_options().set_brush_square(true);
    h.drag(origin.x + 620, origin.y + 60, origin.x + 680, origin.y + 60);
    const int square_ink = h.count_color(612, 40, 688, 80, 255, 0, 0);
    h.tool_options().set_brush_square(false);
    h.drag(origin.x + 620, origin.y + 100, origin.x + 680, origin.y + 100);
    const int round_ink = h.count_color(612, 80, 688, 120, 255, 0, 0);
    h.tool_options().set_brush_spacing(1.0);
    h.drag(origin.x + 620, origin.y + 140, origin.x + 680, origin.y + 140);
    const int dotted_ink = h.count_color(612, 120, 688, 160, 255, 0, 0);
    h.tool_options().set_brush_spacing(0.25);
    CHECK(square_ink > 0);
    CHECK(round_ink > 0);
    CHECK(square_ink > round_ink);
    CHECK(dotted_ink > 0);
    CHECK(dotted_ink < round_ink);

    // 30. The brush tool paints with the picked brush type while the pencil is
    //     not selected, and the pencil keeps the plain tip for the same picked
    //     type. The calligraphy nib is the narrower of the two over one drag.
    h.tool_options().set_brush_kind(EBrushKind::CALLIGRAPHY1);
    h.user().set_selected_tool(ETool::BRUSH);
    h.drag(origin.x + 620, origin.y + 200, origin.x + 680, origin.y + 200);
    const int calligraphy_ink = h.count_color(612, 180, 688, 220, 255, 0, 0);
    h.user().set_selected_tool(ETool::PENCIL);
    h.drag(origin.x + 620, origin.y + 240, origin.x + 680, origin.y + 240);
    const int plain_ink = h.count_color(612, 220, 688, 260, 255, 0, 0);
    CHECK(calligraphy_ink > 0);
    CHECK(plain_ink > calligraphy_ink);


    // 28. The Resize dialog takes a new canvas size as one edit, and one undo
    //     gives the old size back.
    const int before_w = h.controller().document().width();
    const int before_h = h.controller().document().height();
    h.user().invoke_resize_dialog();
    CHECK(h.resize_open());
    h.resize().set_width_text(slint::SharedString("1200"));
    h.resize().set_height_text(slint::SharedString("900"));
    h.resize().invoke_accepted();
    CHECK(!h.resize_open());
    CHECK(h.controller().document().width() == 1200);
    CHECK(h.controller().document().height() == 900);
    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.controller().document().width() == before_w);
    CHECK(h.controller().document().height() == before_h);

    // ... and a size the canvas cannot take is refused with a reason, leaving
    // the dialog up so it can be corrected.
    h.user().invoke_resize_dialog();
    h.resize().set_width_text(slint::SharedString(""));
    h.resize().invoke_accepted();
    CHECK(h.resize_open());
    CHECK(h.message_open());
    h.message().invoke_accepted();
    h.resize().invoke_cancelled();
    CHECK(!h.resize_open());

    // 30. Pasting a picture larger than the canvas grows the canvas to fit,
    //     and one undo restores both the pixels and the size.
    const std::filesystem::path big_path = std::filesystem::temp_directory_path() / "ls_paint_ui_big.png";
    {
        CanvasDocument bigger(1200, 900);
        bigger.active_layer().pixels.at(1190, 890) = Rgba8{255, 0, 0, 255};
        bigger.mark_dirty(bigger.bounds());
        (void)bigger.flush();
        CHECK(save(bigger, big_path, ImageFormat::Png, nullptr));
    }
    const int small_w = h.controller().document().width();
    const int small_h = h.controller().document().height();
    CHECK(small_w < 1200);
    CHECK(h.controller().paste_from_file(big_path).ok);
    CHECK(h.controller().document().width() == 1200);
    CHECK(h.controller().document().height() == 900);
    // The pasted block hovers on the paper until it is applied, so the red
    // corner lives in the block and not yet in the flattened pixels.
    const auto& pasted_block = h.controller().document().floating();
    CHECK(pasted_block.has_value());
    CHECK(pasted_block->position.x == 0 && pasted_block->position.y == 0);
    const Rgba8 red{255, 0, 0, 255};
    CHECK(pasted_block->content.at(1190, 890) == red);
    CHECK(!h.color_near(1190, 890, 255, 0, 0));
    h.controller().history().undo();
    (void)h.controller().document().flush();
    CHECK(h.controller().document().width() == small_w);
    CHECK(h.controller().document().height() == small_h);
    std::filesystem::remove(big_path);

    // 31. Closing a document with unsaved work asks before it goes, and
    //     cancelling keeps the window alive.
    h.user().set_selected_tool(ETool::PENCIL);
    h.move(origin.x + 500, origin.y + 300);
    h.press(origin.x + 500, origin.y + 300);
    h.move(origin.x + 560, origin.y + 300);
    h.release(origin.x + 560, origin.y + 300);
    CHECK(h.doc().get_modified());
    h.window().dispatch_close_requested_event();
    CHECK(h.confirm_open());
    h.confirm().invoke_cancelled();
    CHECK(!h.confirm_open());

    // 32. The Edit Colors dialog opens from the ribbon's button, starts on the
    //     active color, and Cancel changes nothing.
    const slint::Color primary_before = h.user().get_primary_color();
    h.user().set_color_idx(0);
    h.user().set_primary_color(slint::Color::from_rgb_uint8(10, 20, 30));
    h.user().invoke_edit_colors();
    CHECK(h.picker_open());
    CHECK(h.picker().get_current() == slint::Color::from_rgb_uint8(10, 20, 30));
    CHECK(h.picker().get_hex_text() == slint::SharedString("0A141E"));
    h.picker().invoke_cancelled();
    CHECK(!h.picker_open());
    CHECK(h.user().get_primary_color() == slint::Color::from_rgb_uint8(10, 20, 30));
    h.user().set_primary_color(primary_before);

    // 33. The ring picks the hue and the square the saturation and the value.
    const auto near = [](float a, float b) { return std::fabs(a - b) < 0.5f; };
    h.user().invoke_edit_colors();
    h.picker().invoke_ring_picked(0.5f, 0.0f);  // the top of the ring
    CHECK(near(h.picker().get_hue(), 0.0f));
    h.picker().invoke_ring_picked(1.0f, 0.5f);  // the right of the ring
    CHECK(near(h.picker().get_hue(), 90.0f));
    h.picker().invoke_ring_picked(0.5f, 1.0f);  // the bottom of the ring
    CHECK(near(h.picker().get_hue(), 180.0f));
    // The square is white at its left edge and black at its bottom edge.
    h.picker().invoke_square_picked(0.0f, 0.0f);
    CHECK(h.picker().get_saturation() == 0.0f);
    CHECK(near(h.picker().get_value(), 1.0f));
    CHECK(h.picker().get_current() == slint::Color::from_rgb_uint8(255, 255, 255));
    h.picker().invoke_square_picked(1.0f, 1.0f);
    CHECK(near(h.picker().get_saturation(), 1.0f));
    CHECK(h.picker().get_value() == 0.0f);
    CHECK(h.picker().get_current() == slint::Color::from_rgb_uint8(0, 0, 0));

    // 34. The readouts follow the color, a typed color is applied, and OK and
    //     Add to Custom Colors write where they should.
    h.picker().invoke_color_entered(slint::SharedString("00FF00"));
    CHECK(h.picker().get_hex_text() == slint::SharedString("00FF00"));
    CHECK(h.picker().get_red_text() == slint::SharedString("0"));
    CHECK(h.picker().get_green_text() == slint::SharedString("255"));
    CHECK(h.picker().get_blue_text() == slint::SharedString("0"));
    CHECK(near(h.picker().get_hue(), 120.0f));
    // An entry that is not a color leaves the picker on the color it had.
    h.picker().invoke_color_entered(slint::SharedString("chartreuse"));
    CHECK(h.picker().get_hex_text() == slint::SharedString("00FF00"));
    // A tuple reads the same way as a hex value.
    h.picker().invoke_color_entered(slint::SharedString("10, 20, 30"));
    CHECK(h.picker().get_hex_text() == slint::SharedString("0A141E"));
    // OK writes the color into whichever of the two is active.
    h.user().set_color_idx(1);
    h.picker().invoke_accepted();
    CHECK(!h.picker_open());
    CHECK(h.user().get_secondary_color() == slint::Color::from_rgb_uint8(10, 20, 30));
    // Add to Custom Colors fills the slot of the dialog's own grid that it
    // points at, and the next add walks on to the next slot.
    h.user().invoke_edit_colors();
    h.picker().invoke_custom_picked(slint::Color::from_rgb_uint8(1, 2, 3), 3);
    CHECK(h.picker().get_custom_index() == 3);
    h.picker().invoke_add_custom();
    CHECK(h.picker().get_custom_index() == 4);
    const auto customs = h.picker().get_custom_colors();
    CHECK(customs->row_count() == static_cast<std::size_t>(kDialogCustomColorSlots));
    const auto filled = customs->row_data(3);
    CHECK(filled.has_value());
    CHECK(*filled == slint::Color::from_rgb_uint8(1, 2, 3));
    // OK moves the slot the dialog points at into the ribbon's strip, which
    // keeps the last ten colors.
    h.picker().invoke_custom_picked(slint::Color::from_rgb_uint8(1, 2, 3), 3);
    h.picker().invoke_accepted();
    const auto palette = h.user().get_color_palette();
    CHECK(palette->row_count() == static_cast<std::size_t>(kDefaultPalette.size()) +
                                    static_cast<std::size_t>(kCustomColorSlots));
    const auto head = palette->row_data(static_cast<std::size_t>(kDefaultPalette.size()));
    CHECK(head.has_value());
    CHECK(*head == slint::Color::from_rgb_uint8(1, 2, 3));
    h.picker().invoke_cancelled();

    // 35. The Text tab's font list is the families the backend can draw with,
    //     and each entry names one of them.
    const auto families = h.font_style().get_families();
    CHECK(families->row_count() > 0);
    const auto first_family = families->row_data(0);
    CHECK(first_family.has_value());
    CHECK(!first_family->empty());

    // 36. The ruler and grid shortcuts turn their layers on and off.
    h.user().set_show_rulers(false);
    h.press_ctrl();
    h.key("r");
    h.release_ctrl();
    CHECK(h.user().get_show_rulers());
    h.press_ctrl();
    h.key("r");
    h.release_ctrl();
    CHECK(!h.user().get_show_rulers());
    h.user().set_show_gridlines(false);
    h.press_ctrl();
    h.key("g");
    h.release_ctrl();
    CHECK(h.user().get_show_gridlines());
    h.press_ctrl();
    h.key("g");
    h.release_ctrl();
    CHECK(!h.user().get_show_gridlines());

    // The stored window geometry is applied while the window is made, before
    // anything shows it. A window is built here directly, because the harness
    // resizes every window it makes.
    {
        const std::filesystem::path geometry_dir =
            std::filesystem::temp_directory_path() / "lsp_ui_geometry";
        std::error_code geometry_error;
        std::filesystem::remove_all(geometry_dir, geometry_error);
#if defined(_WIN32)
        _putenv_s("APPDATA", geometry_dir.string().c_str());
#else
        setenv("XDG_CONFIG_HOME", geometry_dir.string().c_str(), 1);
#endif
        {
            SQLiteStore seeded(default_store_path());
            seeded.set(kSTORE_KEY_WINDOW_X, "60");
            seeded.set(kSTORE_KEY_WINDOW_Y, "40");
            seeded.set(kSTORE_KEY_WINDOW_WIDTH, "744");
            seeded.set(kSTORE_KEY_WINDOW_HEIGHT, "522");
            seeded.save();
        }
        auto window = AppWindow::create();
        AppController controller;
        UiState ui(*window, controller);
        const slint::PhysicalSize size = window->window().size();
        CHECK(size.width == 744);
        CHECK(size.height == 522);
        // The stored position is applied the same way, but the testing backend
        // is off-screen and reports no window position, so it cannot be read
        // back here.
    }

    // The settings store: one run writes the window size, the three toggles
    // and the font, and the next run reads them back. It runs against a config
    // directory of its own, so this is the last thing the suite does.
    {
        const std::filesystem::path round_trip_dir =
            std::filesystem::temp_directory_path() / "lsp_ui_config_round_trip";
        std::error_code round_trip_error;
        std::filesystem::remove_all(round_trip_dir, round_trip_error);
#if defined(_WIN32)
        _putenv_s("APPDATA", round_trip_dir.string().c_str());
#else
        setenv("XDG_CONFIG_HOME", round_trip_dir.string().c_str(), 1);
#endif
        {
            Harness first;
            first.user().set_show_rulers(true);
            first.user().set_show_gridlines(true);
            first.user().set_show_status_bar(false);
            first.font_style().set_family("DejaVu Serif");
            // Closing the window is what writes the settings out.
            first.window().dispatch_close_requested_event();
        }
        {
            Harness second;
            // Rulers are the harness's own business: it turns them off for
            // every window it makes so the drawing checks measure the view.
            // That toggle is checked through the store below instead.
            CHECK(second.user().get_show_gridlines());
            CHECK(!second.user().get_show_status_bar());
            CHECK(std::string(second.font_style().get_family()) == "DejaVu Serif");
        }
        // The settings landed under that directory, and the window size the
        // tests drive was written with them.
        const std::filesystem::path store_path = default_store_path();
        CHECK(store_path.parent_path() == round_trip_dir / kAPP_ID);
        SQLiteStore stored(store_path);
        CHECK(stored.get(kSTORE_KEY_SHOW_RULERS).value_or("") == "1");
        CHECK(stored.get(kSTORE_KEY_WINDOW_WIDTH).value_or("") == "900");
        CHECK(stored.get(kSTORE_KEY_WINDOW_HEIGHT).value_or("") == "600");
        CHECK(stored.get(kSTORE_KEY_FONT_FAMILY).value_or("") == "DejaVu Serif");
    }

    // 37. The File menu's recent list: the settings store fills it, a click
    //     opens that file, an entry whose file is gone leaves the list without
    //     a word, and opening over unsaved work asks the same question File >
    //     Open asks.
    {
        const std::filesystem::path recent_dir =
            std::filesystem::temp_directory_path() / "lsp_ui_recent";
        std::filesystem::remove_all(recent_dir);
        std::filesystem::create_directories(recent_dir);
#ifdef _WIN32
        _putenv_s("APPDATA", recent_dir.string().c_str());
#else
        setenv("XDG_CONFIG_HOME", recent_dir.string().c_str(), 1);
#endif

        const std::filesystem::path picture = recent_dir / "recent.png";
        {
            Harness writer;
            writer.user().set_selected_tool(ETool::PENCIL);
            writer.press(300, 300);
            writer.drag(300, 300, 340, 340);
            writer.release(340, 340);
            CHECK(writer.controller().save_document(picture, ImageFormat::Png).ok);
            CHECK(std::filesystem::exists(picture));
        }

        // The entry for a file that is no longer there sits first.
        const std::filesystem::path gone = recent_dir / "gone.png";
        CHECK(!std::filesystem::exists(gone));
        {
            SQLiteStore seeded(default_store_path());
            seeded.set(kSTORE_KEY_RECENT_FILES, encode_list({gone.string(), picture.string()}));
            seeded.save();
        }

        Harness h;
        CHECK(h.user().get_recent_files()->row_count() == 2);
        const std::optional<slint::SharedString> first = h.user().get_recent_files()->row_data(0);
        CHECK(first.has_value() && std::string(*first) == gone.string());

        // The File tab opens the panel; its first entry is that missing file.
        h.press(25, 14);
        h.release(25, 14);
        CHECK(h.popups() == 1);
        h.press(419, 71);
        h.release(419, 71);
        CHECK(h.popups() == 0);
        CHECK(h.user().get_recent_files()->row_count() == 1);
        CHECK(!h.message_open());
        CHECK(!h.controller().has_current_path());

        // The entry left behind opens its picture.
        h.press(25, 14);
        h.release(25, 14);
        CHECK(h.popups() == 1);
        h.press(419, 71);
        h.release(419, 71);
        CHECK(h.popups() == 0);
        CHECK(h.controller().has_current_path());
        CHECK(h.controller().current_path() == picture);
        CHECK(h.doc().get_title() == "recent.png");
        CHECK(!h.controller().history().is_modified());

        // Opening over unsaved work asks first, and Don't Save goes through.
        h.user().set_selected_tool(ETool::PENCIL);
        // A point on the paper, found rather than assumed: the view's own
        // scroll decides where the document's top left lands.
        float canvas_x = 0;
        float canvas_y = 0;
        for (float probe = 20; probe < 400; probe += 20) {
            h.move(probe, probe);
            if (!h.user().get_cursor_on_canvas()) continue;
            canvas_x = probe;
            canvas_y = probe;
            break;
        }
        CHECK(h.user().get_cursor_on_canvas());
        h.user().set_primary_color(slint::Color::from_rgb_uint8(0, 0, 255));
        h.press(canvas_x, canvas_y);
        h.drag(canvas_x, canvas_y, canvas_x + 40, canvas_y + 40);
        h.release(canvas_x + 40, canvas_y + 40);
        CHECK(h.count_color(0, 0, h.doc().get_canvas_width(), h.doc().get_canvas_height(), 0, 0, 255) > 0);
        CHECK(h.doc().get_modified());
        h.press(25, 14);
        h.release(25, 14);
        h.press(419, 71);
        h.release(419, 71);
        CHECK(h.confirm_open());
        CHECK(!h.confirm().get_message().empty());
        h.confirm().invoke_discarded();
        CHECK(h.controller().has_current_path());
        CHECK(h.controller().current_path() == picture);
        CHECK(!h.controller().history().is_modified());
    }

    // 38. The custom colours and the last folder the dialogs used survive a
    // restart.
    const std::filesystem::path customs_dir =
        std::filesystem::temp_directory_path() / "lsp_ui_customs";
    std::filesystem::remove_all(customs_dir);
    std::filesystem::create_directories(customs_dir);
#if defined(_WIN32)
    _putenv_s("APPDATA", customs_dir.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", customs_dir.string().c_str(), 1);
#endif
    const std::filesystem::path custom_picture = customs_dir / "custom.png";
    {
        Harness writer;
        CHECK(writer.controller().save_document(custom_picture, ImageFormat::Png).ok);
    }
    {
        SQLiteStore seeding(default_store_path());
        seeding.set(kSTORE_KEY_PALETTE_CUSTOMS, encode_list({"112233", "445566"}));
        seeding.set(kSTORE_KEY_DIALOG_CUSTOMS, encode_list({"778899"}));
        seeding.set(kSTORE_KEY_RECENT_FILES,
                    encode_list({custom_picture.string(), "older-1.png", "older-2.png", "older-3.png",
                                 "older-4.png", "older-5.png", "older-6.png", "older-7.png",
                                 "older-8.png", "older-9.png", "older-10.png", "older-11.png"}));
        seeding.save();
    }
    {
        Harness seeded;
        const auto palette = seeded.user().get_color_palette();
        // The list behind the File menu keeps the ten newest entries.
        CHECK(seeded.user().get_recent_files()->row_count() ==
              static_cast<std::size_t>(kMaxRecentFiles));
        CHECK(palette->row_data(static_cast<std::size_t>(kDefaultPalette.size())) ==
              slint::Color::from_rgb_uint8(0x11, 0x22, 0x33));
        CHECK(palette->row_data(static_cast<std::size_t>(kDefaultPalette.size()) + 1) ==
              slint::Color::from_rgb_uint8(0x44, 0x55, 0x66));
        // The Edit Colors grid keeps its own slots, seeded from the store.
        seeded.user().invoke_edit_colors();
        const auto customs = seeded.picker().get_custom_colors();
        CHECK(customs->row_data(0) == slint::Color::from_rgb_uint8(0x77, 0x88, 0x99));
        seeded.picker().invoke_cancelled();

        // Opening a picture records the folder the dialogs should start in.
        seeded.press(25, 14);
        seeded.release(25, 14);
        seeded.press(419, 71);
        seeded.release(419, 71);
        CHECK(seeded.controller().current_path() == custom_picture);
        SQLiteStore stored_again(default_store_path());
        CHECK(stored_again.get(kSTORE_KEY_LAST_DIRECTORY) == custom_picture.parent_path().string());

        // Closing writes the settings back, and the colours must come out the
        // way they went in rather than drifting through the hex conversion.
        seeded.window().dispatch_close_requested_event();
        SQLiteStore written(default_store_path());
        const std::optional<std::string> written_customs =
            written.get(kSTORE_KEY_PALETTE_CUSTOMS);
        CHECK(written_customs.has_value());
        const std::vector<std::string> written_entries =
            written_customs ? decode_list(*written_customs) : std::vector<std::string>{};
        CHECK(written_entries.size() == static_cast<std::size_t>(kCustomColorSlots));
        CHECK(!written_entries.empty() && written_entries[0] == "112233");
        CHECK(written_entries.size() > 1 && written_entries[1] == "445566");
    }

    // 39. A gesture owns the pointer until it ends: the other button abandons
    // it without painting, a release of the wrong button does not cut a stroke
    // short, Escape drops a block that is still hovering, and a tool switch
    // abandons a marquee instead of letting the next move fill it.
    {
        Harness h;
        float canvas_x = 0.0f;
        float canvas_y = 0.0f;
        for (float probe = 40.0f; probe < 600.0f; probe += 20.0f) {
            h.move(probe, probe);
            if (h.user().get_cursor_on_canvas()) {
                canvas_x = probe;
                canvas_y = probe;
                break;
            }
        }
        CHECK(h.user().get_cursor_on_canvas());

        // The other button takes the pointer back: nothing reaches the layer
        // and no undo step is recorded.
        h.user().set_selected_tool(ETool::PENCIL);
        const PixelBuffer untouched = h.controller().document().active_layer().pixels;
        h.press(canvas_x, canvas_y);
        h.move(canvas_x + 20.0f, canvas_y + 6.0f);
        h.press(canvas_x + 40.0f, canvas_y + 12.0f, PointerEventButton::Right);
        CHECK(!h.controller().input().active());
        h.move(canvas_x + 60.0f, canvas_y + 18.0f);
        h.release(canvas_x + 60.0f, canvas_y + 18.0f, PointerEventButton::Right);
        CHECK((h.controller().document().active_layer().pixels == untouched));
        CHECK(!h.controller().history().is_modified());

        // Only the button that started a stroke ends it.
        h.press(canvas_x, canvas_y);
        h.move(canvas_x + 24.0f, canvas_y + 8.0f);
        h.release(canvas_x + 24.0f, canvas_y + 8.0f, PointerEventButton::Right);
        CHECK(h.controller().input().active());
        h.release(canvas_x + 24.0f, canvas_y + 8.0f);
        CHECK(!h.controller().input().active());
        CHECK(h.controller().history().is_modified());
        CHECK((h.controller().document().active_layer().pixels != untouched));

        // Escape drops a block that is still hovering. The paste never lands,
        // so it adds no undo step of its own.
        h.controller().select_all();
        h.user().invoke_transform_selection_content(EContentTransform::ROTATE180);
        CHECK(h.controller().document().floating().has_value());
        const bool modified_before_escape = h.controller().history().is_modified();
        h.key("\x1b");
        CHECK(!h.controller().document().floating().has_value());
        CHECK(h.controller().history().is_modified() == modified_before_escape);

        // A tool switch abandons a marquee in flight, so the hover that follows
        // cannot fill a selection the drag never finished.
        h.user().set_selected_tool(ETool::SELECTION);
        h.controller().clear_selection();
        h.press(canvas_x - 40.0f, canvas_y - 40.0f);
        h.move(canvas_x, canvas_y);
        h.user().set_selected_tool(ETool::PENCIL);
        h.user().set_selected_tool(ETool::SELECTION);
        h.move(canvas_x + 40.0f, canvas_y + 40.0f);
        CHECK(!h.controller().document().selection().active());

        // A shape drag is a preview until its release, so the same button takes
        // the preview back and leaves the layer alone.
        h.user().set_selected_tool(ETool::SHAPE);
        const PixelBuffer before_shape = h.controller().document().active_layer().pixels;
        h.press(canvas_x, canvas_y);
        h.move(canvas_x + 40.0f, canvas_y + 30.0f);
        CHECK(h.overlay().get_visible());
        h.press(canvas_x + 40.0f, canvas_y + 30.0f, PointerEventButton::Right);
        CHECK(!h.overlay().get_visible());
        h.release(canvas_x + 40.0f, canvas_y + 30.0f, PointerEventButton::Right);
        CHECK((h.controller().document().active_layer().pixels == before_shape));
    }

    std::filesystem::remove(save_path);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d UI check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("UI OK\n");
    return 0;
}

}  // namespace lsp

int main() { return lsp::run_ui_tests(); }
