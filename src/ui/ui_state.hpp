#pragma once

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
};

}  // namespace lsp
