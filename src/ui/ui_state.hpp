#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "app/app_controller.hpp"
#include "appwindow.h"
#include "private/slint_timer.h"
#include "render/slint_bridge.hpp"

namespace lsp {

class UiState {
public:
    UiState(AppWindow& window, AppController& controller);

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
    // Closes the open text box, drawing its content into the paper. Safe to
    // call when no box is open: the editor's own commit, a click elsewhere and
    // a tool switch all land here.
    // True while a text box is being edited. The tool owns the flag, so the
    // editor's Slint side cannot get out of step with it.
    [[nodiscard]] bool text_open() const noexcept;
    void do_commit_text(const std::string& text);
    void refresh();

    // File actions, each of which may raise one of the modal surfaces first.
    enum class PendingAction { None, Exit, New, Open };

    void do_file_new();
    void do_file_open();
    void do_file_save();
    void do_file_save_as();
    void do_file_exit();
    void do_paste_from_file();

    // Work left half-done by a question the user has to answer.
    void ask_unsaved(PendingAction action);
    void run_pending();

    // The picker: which question it is asking, where it is looking, and the
    // listing it is showing.
    void show_file_dialog(EFileDialogMode mode);
    void refresh_file_dialog();
    void do_file_up();
    void do_file_navigate(int row);
    void do_file_filter_changed(int index);
    void do_file_ok();
    void do_file_cancel();

    // The canvas size dialog: the current size is offered, and only a size
    // inside the canvas limits is applied.
    void do_open_resize_dialog();
    void do_resize_ok();
    void do_resize_cancel();

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
    slint::Timer m_ants_timer;
    int m_ant_phase = 0;
    bool m_ants_visible = false;
    bool m_text_open = false;
    // Timed here because Slint's pointer events carry no click count.
    std::chrono::steady_clock::time_point m_last_press;
    Point2f m_last_press_position;
    bool m_have_last_press = false;
    // Watched in refresh() so a tool that is left mid-gesture can release it.
    ToolId m_last_tool = ToolId::Pencil;

    PendingAction m_pending = PendingAction::None;
    std::filesystem::path m_file_dir;
    std::vector<std::filesystem::path> m_listing;
};

}  // namespace lsp
