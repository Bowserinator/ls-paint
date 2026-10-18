#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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

namespace lsp {

class UiState {
public:
    UiState(AppWindow& window, AppController& controller);

    // Puts the remembered window size and place back. The constructor runs this
    // before the window is first shown; main() runs it again once the window
    // exists, because a position set too early is what some backends drop.
    void restore_settings();

    // Opens the document the program was started with. False, after saying
    // so, when it cannot be opened.
    bool open_startup_file(const std::filesystem::path& path);

    // The Edit Colors window, made on first use. Its state lives in the window
    // itself, so the tests read it here rather than from a global.
    [[nodiscard]] lsp_colors::EditColorsWindow& color_window();
    // True while the Edit Colors window is on screen.
    [[nodiscard]] bool color_window_open() const noexcept;

    // The unsaved-changes question, made on first use. It is a real window, so
    // the tests read its message and press its buttons here rather than through
    // a global.
    [[nodiscard]] lsp_confirm::ConfirmDialog& confirm_window();
    // True while that question is on screen.
    [[nodiscard]] bool confirm_window_open() const noexcept;

    // A plain message, and the canvas size question. Both are real windows too.
    [[nodiscard]] lsp_message::MessageDialog& message_window();
    [[nodiscard]] bool message_window_open() const noexcept;
    [[nodiscard]] lsp_resize::ResizeDialog& resize_window();
    [[nodiscard]] bool resize_window_open() const noexcept;

private:
    // Pulls the Slint color pickers into the controller. Called before every
    // operation that needs them: only pointer presses synced them before, so
    // a cut or a rotate would erase with whatever color was picked last.
    void sync_colors();
    // Pulls the Shapes, Outline/Fill and thickness dropdowns into the tool
    // settings the shape tool reads when it starts a gesture.
    void sync_shape_style();
    // Pulls the font controls into the settings the text commit rasterizes.
    void sync_font_style();
    // Publishes the family list, narrowed to the names that contain `query`.
    void publish_font_families(std::string_view query);
    // Publishes the width of every line of the text the box holds, so the
    // overlay's underline and strikethrough rules stop at the end of their line.
    void publish_line_widths(std::string_view text);
    // Publishes the paper colour under the pointer for the status bar, blank
    // while the pointer is off the paper.
    void publish_hover_color();
    void publish_cursor_overlay();
    // True while a text box is being edited. The tool owns the flag, so the
    // editor's Slint side cannot get out of step with it.
    [[nodiscard]] bool text_open() const noexcept;
    // Closes the open text box, drawing its content into the paper. Safe to
    // call when no box is open: the editor's own commit, a click elsewhere and
    // a tool switch all land here.
    void do_commit_text(const std::string& text);
    void do_cancel_text();
    void refresh();

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

    // The canvas size dialog: the current size is offered, and only a size
    // inside the canvas limits is applied.
    void do_open_resize_dialog();
    void do_resize_ok();

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
    int m_ant_phase = 0;
    bool m_ants_visible = false;
    bool m_rotate_hover = false;
    bool m_rotate_active = false;
    bool m_text_was_visible = false;
    // Whether the system clipboard last offered an image, for the Paste commands.
    bool m_system_clipboard = false;
    // Timed here because Slint's pointer events carry no click count.
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
