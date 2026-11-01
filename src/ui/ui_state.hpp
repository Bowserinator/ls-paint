#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "about-window.h"
#include "app/app_controller.hpp"
#include "appwindow.h"
#include "confirm-dialog.h"
#include "edit-colors-window.h"
#include "message-dialog.h"
#include "resize-dialog.h"
#include "store/store.hpp"
#include "private/slint_timer.h"
#include "render/slint_bridge.hpp"
#include "ui/debug_stats.hpp"
#include "utils/trace.hpp"

namespace lsp {

class UiState {
public:
    UiState(AppWindow& window, AppController& controller);
    ~UiState();

    // Puts the remembered window size and place back. The constructor runs this
    // before the window is first shown; main() runs it again once the window
    // exists, because a position set too early is what some backends drop.
    void restore_settings();

    // Opens the document the program was started with. False, after saying
    // so, when it cannot be opened.
    bool open_startup_file(const std::filesystem::path& path);

    // The Edit Colors window, made on first use.
    [[nodiscard]] lsp_colors::EditColorsWindow& color_window();
    // True while the Edit Colors window is on screen.
    [[nodiscard]] bool color_window_open() const noexcept;

    // The unsaved-changes question, made on first use.
    [[nodiscard]] lsp_confirm::ConfirmDialog& confirm_window();
    // True while that question is on screen.
    [[nodiscard]] bool confirm_window_open() const noexcept;

    // A plain message, and the canvas size question.
    [[nodiscard]] lsp_message::MessageDialog& message_window();
    [[nodiscard]] bool message_window_open() const noexcept;
    [[nodiscard]] lsp_resize::ResizeDialog& resize_window();
    [[nodiscard]] bool resize_window_open() const noexcept;

private:
    // Pulls the picked colors into the controller.
    void sync_colors();
    // Pulls the shape, outline, fill and thickness choices into the shape tool's options.
    void sync_shape_style();
    // Pulls the font controls into the text options.
    void sync_font_style();
    // Publishes the family list, narrowed to the names that contain `query`.
    void publish_font_families(std::string_view query);
    // Publishes how the text the box holds will look once committed, and where
    // its cursor stands.
    void publish_text_preview(std::string_view text);
    // Publishes the paper colour under the pointer for the status bar, blank
    // while the pointer is off the paper.
    void publish_hover_color();
    void publish_cursor_overlay();
    // True while a text box is being edited.
    [[nodiscard]] bool text_open() const noexcept;
    // Closes the open text box, drawing its content into the paper. Safe to
    // call when no box is open: the editor's own commit, a click elsewhere and
    // a tool switch all land here.
    void do_commit_text(const std::string& text);
    void do_cancel_text();
    // Tells the ribbon that a box was closed without writing any text.
    void report_text_cancelled();
    // Shows `grips` on the paper.
    void publish_grips(std::vector<SelectionGrip> grips);
    void publish_ant_edges(const RectI& box, const std::optional<RectI>& clip);
    void clear_ant_edges();
    void clear_ant_dashes();
    // The bytes of the open text that the user has selected, an empty span when
    // nothing is.
    [[nodiscard]] std::pair<std::size_t, std::size_t> text_selection() const;
    // A style button was pressed: the selected text changes, or all of the text
    // when nothing is selected.
    void do_toggle_style(EFontToggle toggle);
    // A colour was picked: the selected text takes it, or all of the text when
    // nothing is selected.
    void do_recolor_text(const slint::Color& color);
    // Makes the style buttons show the style of the selected text, or of the
    // character before the cursor when nothing is selected.
    void show_text_style();
    void refresh();
    void refresh_throttled(bool hover_only);
    void refresh_move();
    void refresh_hover();
    void apply_zoom(float zoom);
    void start_debug();
    void publish_debug();
    void update_canvas();
    void publish_canvas();
    // Whether a stroke is being painted right now.
    [[nodiscard]] bool live_stroke();
    // Shows what a stroke just painted in `region` without touching the canvas.
    void show_stroke_edit(const PixelBuffer& paper, RectI region);
    // Moves everything shown for the stroke into the canvas.
    void settle_stroke();

    // What the store holds: the window geometry and the settings the app
    // remembers, as one comparable set.
    struct SettingsSnapshot {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool rulers = false;
        bool gridlines = false;
        bool status_bar = true;
        bool ribbon = true;
        std::string family;

        friend bool operator==(const SettingsSnapshot&, const SettingsSnapshot&) = default;
    };

    // The settings of a previous run, each one taken only when the store holds
    // it and it parses.
    void load_settings();
    // Writes the settings the store does not hold yet.
    void save_settings();
    // Compares the window and the settings with the stored ones and saves when
    // they differ. The window cannot announce a move, so a timer drives this.
    void watch_settings();
    [[nodiscard]] SettingsSnapshot current_settings() const;
    // Where a file dialog opens: the folder the last dialog used, or beside the
    // document when no dialog has run yet.
    [[nodiscard]] std::filesystem::path dialog_directory() const;

    // Writes one entry of the ribbon's palette.
    void set_palette_color(int index, const slint::Color& color);
    // The grid of custom colors the Edit Colors dialog edits. The ribbon's strip
    // seeds it, the dialog changes it, and OK moves one slot into the strip.
    void seed_dialog_customs();
    void write_dialog_custom(int index, const slint::Color& color);
    void rotate_strip(const slint::Color& color);

    // The Edit Colors picker. The hue, saturation and value it holds are the
    // colour being chosen, and every other field it shows follows from them.
    void ensure_color_window();
    // The window the picker's callbacks come from, or nothing when it has never
    // been made. Every one of those callbacks is only reached while it exists.
    [[nodiscard]] lsp_colors::EditColorsWindow* color_picker();
    void do_open_color_dialog();
    void do_color_accepted();
    void close_color_window();
    // Hides a dialog that may never have been made, and returns focus to the
    // main window.
    template <typename Handle>
    void hide_dialog(std::optional<Handle> dialog);
    void do_color_add_custom();
    void do_color_basic_picked(const slint::Color& color);
    void do_color_custom_picked(const slint::Color& color, int index);
    void do_color_ring_picked(float x, float y);
    void do_color_square_picked(float x, float y);
    void do_color_entered(const slint::SharedString& text);
    void do_color_hex_edited(const slint::SharedString& text);
    void push_color_dialog();

    // The About window, made on first use.
    void ensure_about_window();
    void do_open_about();

    // The unsaved-changes question, made on first use.
    void ensure_confirm_window();
    void close_confirm_window();

    // The plain message and the canvas size question, made on first use.
    void ensure_message_window();
    void close_message_window();
    void ensure_resize_window();
    void close_resize_window();

    // Puts a dialog in the middle of the main window. A dialog is a window of
    // its own, so its place on screen comes from here. The size it has is only
    // known once it is on screen, so call this after showing it.
    void center_dialog(slint::Window& dialog) const;

    // File actions, each of which may ask a question first.
    enum class PendingAction { None, Exit, New, Open, OpenRecent };

    void do_file_new();
    void do_file_open();
    void do_file_exit();
    void do_paste();
    void do_paste_from_file();
    // Asks the system clipboard whether it holds an image, so the Paste commands
    // know whether to offer themselves. Run when the ribbon changes tab.
    void check_clipboard();

    // The File menu's recent list. A path is added when a document is opened or
    // saved, and the list is kept newest first, without repeats.
    void note_recent_file(const std::filesystem::path& path);
    void forget_recent_file(const std::filesystem::path& path);
    void publish_recent_files();
    // Opens the entry the menu named, through the same question as File > Open.
    void open_recent(int index);
    // Opens a document and records it. False, after saying so, when it fails.
    bool open_path(const std::filesystem::path& path);

    // Work left half-done by a question the user has to answer.
    void ask_unsaved(PendingAction action);
    void run_pending();

    // Saves under the current name, asking for one first when there is none.
    // Returns whether the document reached disk.
    bool save_document();
    bool save_document_as();
    void open_document();
    // False, after saying so, when the system has no file dialog to show.
    bool ensure_file_dialogs();

    // The Resize and Skew dialog: only a size inside the canvas limits and a
    // shear the picture can take are applied.
    void do_open_resize_dialog();
    void do_resize_ok();
    void do_resize_unit_changed();
    // `from_horizontal` says which resize field was typed into.
    void do_resize_link(bool from_horizontal);

    void show_message(const std::string& title, const std::string& text);
    // Pushes the file name and the unsaved marker into the window title.
    void set_title();
    // Puts the view back to a state that suits a document nobody has seen yet.
    void after_document_change();
    // Rebuilds the marching-ants outline and the grip positions from the
    // current selection. Driven by refresh() and by the animation timer.
    void update_ants();

    AppWindow& m_window;
    AppController& m_controller;
    SlintBridge m_bridge;
    std::shared_ptr<slint::VectorModel<CanvasTile>> m_canvas_tiles;
    std::vector<TilePlacement> m_canvas_shown;
    std::shared_ptr<slint::VectorModel<StrokePatch>> m_stroke_patches;
    std::vector<RectI> m_stroke_edits;
    std::size_t m_stroke_pixels = 0;
    slint::Timer m_stroke_idle_timer;
    // The dialogs are windows of their own, made on first use and kept for the
    // life of the application.
    std::optional<slint::ComponentHandle<lsp_colors::EditColorsWindow>>
            m_color_window;
    std::optional<slint::ComponentHandle<lsp_about::AboutWindow>> m_about_window;
    std::optional<slint::ComponentHandle<lsp_confirm::ConfirmDialog>>
            m_confirm_window;
    std::optional<slint::ComponentHandle<lsp_message::MessageDialog>>
            m_message_window;
    std::optional<slint::ComponentHandle<lsp_resize::ResizeDialog>>
            m_resize_window;
    // The size the Resize and Skew percentages are of.
    int m_resize_base_width = 0;
    int m_resize_base_height = 0;
    // The settings the app carries from one run to the next, and the values it
    // was last given. An unopenable database leaves every setting at its default
    // and the app otherwise untouched.
    std::unique_ptr<Store> m_settings;
    slint::Timer m_settings_timer;
    SettingsSnapshot m_stored_settings;
    // Kept so the colour picker can write into the palette's custom slots.
    std::shared_ptr<slint::VectorModel<slint::Color>> m_palette;
    // Every family the text tool can draw with, before the filter box narrows it.
    std::vector<std::string> m_font_families;
    // The Edit Colors dialog's own custom colors, shared with the dialog while it
    // is open.
    std::shared_ptr<slint::VectorModel<slint::Color>> m_dialog_customs;
    slint::Timer m_ants_timer;
    bool m_debug = false;
    DebugStats m_debug_stats;
    slint::Timer m_debug_timer;
    slint::Timer m_lag_timer;
    DebugStats::Clock::time_point m_lag_expected;
    DebugStats::Clock::time_point m_debug_totals_at;
    unsigned m_debug_totals[trace::CounterCount] = {};
    // Pointer moves refresh the view at most once per frame.
    slint::Timer m_move_timer;
    std::chrono::steady_clock::time_point m_last_refresh;
    bool m_move_pending = false;
    bool m_move_hover_only = false;
    // What the cursor marks and the colour readout were last built from, so a
    // hover that changes nothing the eye can see costs nothing to show.
    PixelBuffer m_overlay_pixels;
    int m_overlay_reach = 0;
    std::uint64_t m_paper_serial = 0;
    std::uint64_t m_hover_serial = 0;
    int m_hover_x = 0;
    int m_hover_y = 0;
    bool m_hover_valid = false;
    int m_ant_phase = 0;
    // What the outline on screen was last built from.
    std::vector<float> m_ants_key;
    bool m_ants_visible = false;
    bool m_ants_dashes_shown = false;
    struct AntEdge {
        int x = 0;
        int y = 0;
        int length = 0;
        bool horizontal = true;
        int first_step = 0;
        int sign = 1;
        std::vector<std::optional<slint::Image>> frames;
    };
    std::vector<AntEdge> m_ant_edges;
    std::vector<int> m_ant_edges_key;
    int m_ant_edges_phase = -1;
    std::shared_ptr<slint::VectorModel<SelectionEdge>> m_ant_edge_model;
    bool m_rotate_hover = false;
    bool m_rotate_active = false;
    bool m_text_was_visible = false;
    std::string m_text_preview_key;
    std::string m_text_last;
    float m_text_auto_width = 0.0f;
    float m_text_auto_height = 0.0f;
    bool m_move_hover = false;
    bool m_move_active = false;
    std::vector<float> m_text_grips_key;
    std::shared_ptr<slint::VectorModel<SelectionGrip>> m_grips;
    std::uint64_t m_shape_preview_revision = 0;
    // Whether the system clipboard last offered an image, for the Paste commands.
    bool m_system_clipboard = false;
    // Used to detect double clicks.
    std::chrono::steady_clock::time_point m_last_press;
    Point2f m_last_press_position;
    bool m_have_last_press = false;
    // Watched in refresh() so a tool that is left mid-gesture can release it.
    ToolId m_last_tool = ToolId::Pencil;

    PendingAction m_pending = PendingAction::None;
    // The list behind the File menu's recent column, newest first.
    std::vector<std::filesystem::path> m_recent_files;
    // The path a pending open is waiting for, while a question is on screen.
    std::filesystem::path m_pending_path;
    // The folder the last file dialog used, and the custom colours the two grids
    // held when the app last wrote them.
    std::filesystem::path m_last_directory;
    std::vector<slint::Color> m_dialog_customs_stored;
    // Set when a custom colour changes, so the next timer tick writes it.
    bool m_colors_dirty = false;
};

}  // namespace lsp
