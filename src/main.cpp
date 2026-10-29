#include "app/app_controller.hpp"
#include "app/command_line.hpp"
#include "appwindow.h"
#include "ui/ui_state.hpp"

#ifdef _WIN32
#include <stdlib.h>

#include <windows.h>
#endif

int main(int argc, char** argv) {
    auto window = AppWindow::create();
    lsp::AppController controller;
    lsp::UiState ui(*window, controller);
    // Window managers match the window to its desktop entry by this id.
    slint::set_xdg_app_id("ls_paint");
    // Some backends drop a position set before the window is shown.
    window->show();
    ui.restore_settings();
    if (const auto file = lsp::startup_file(argc, argv)) (void)ui.open_startup_file(*file);
    window->run();
    return 0;
}

#ifdef _WIN32
// The program runs without a console, so the system starts it here whichever
// toolchain linked it.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
