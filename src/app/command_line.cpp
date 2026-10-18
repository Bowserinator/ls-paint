#include "app/command_line.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace lsp {

std::optional<std::filesystem::path> startup_file(int argc, char** argv) {
#ifdef _WIN32
    // The narrow arguments are in the ANSI code page, which loses any
    // character outside it, so the wide command line is read instead.
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (wide == nullptr) return std::nullopt;
    std::optional<std::filesystem::path> file;
    if (count > 1 && wide[1][0] != L'\0') file = std::filesystem::path(wide[1]);
    ::LocalFree(wide);
    return file;
#else
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') return std::filesystem::path(argv[1]);
    return std::nullopt;
#endif
}

}  // namespace lsp
