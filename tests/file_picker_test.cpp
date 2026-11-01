#include <cstdio>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "portable-file-dialogs.h"

namespace {

int failures = 0;
int paints = 0;
bool repaint = true;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

class Executor : public pfd::internal::executor {
public:
    using pfd::internal::executor::ready;
};

LRESULT CALLBACK paint_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        ++paints;
        if (repaint) PostMessageW(window, WM_PAINT, 0, 0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int main() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = paint_window;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"LspFilePickerTest";
    if (!RegisterClassW(&window_class)) {
        std::printf("FAIL RegisterClassW\n");
        return 1;
    }
    const HWND window = CreateWindowW(window_class.lpszClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) {
        std::printf("FAIL CreateWindowW\n");
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }

    std::promise<void> finish;
    const std::future<void> gate = finish.get_future();
    Executor dialog;
    dialog.start_func([&gate](int*) {
        gate.wait();
        return std::string{};
    });

    // Winit reposts paints while the callback that opened the picker is active.
    CHECK(PostMessageW(window, WM_PAINT, 0, 0));
    CHECK(!dialog.ready(0));
    CHECK(paints == 1);
    repaint = false;
    MSG message{};
    while (PeekMessageW(&message, window, WM_PAINT, WM_PAINT, PM_REMOVE))
        DispatchMessageW(&message);

    PostQuitMessage(37);
    CHECK(!dialog.ready(0));
    CHECK(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    CHECK(message.wParam == 37);

    finish.set_value();
    CHECK(dialog.result().empty());
    for (int attempt = 0; attempt < 32; ++attempt) {
        dialog.start_func([](int*) { return std::string("Saved"); });
        CHECK(dialog.result() == "Saved");
    }

    DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, instance);
    return failures == 0 ? 0 : 1;
}
