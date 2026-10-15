#pragma once

#include <chrono>
#include <string>

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
};

}  // namespace lsp
