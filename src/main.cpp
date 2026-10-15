#include "app/app_controller.hpp"
#include "appwindow.h"
#include "ui/ui_state.hpp"

int main() {
    auto window = AppWindow::create();
    lsp::AppController controller;
    lsp::UiState ui(*window, controller);
    window->run();
    return 0;
}
