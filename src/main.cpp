#include "app/app_controller.hpp"
#include "appwindow.h"
#include "ui/ui_state.hpp"

int main() {
    auto window = AppWindow::create();
    lsp::AppController controller;
    lsp::UiState ui(*window, controller);
    // The window is shown first and the remembered place put back after: the
    // size is already set from the constructor, so nothing jumps, while a
    // position asked for before the window existed is what some backends drop.
    window->show();
    ui.restore_settings();
    window->run();
    return 0;
}
