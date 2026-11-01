#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace lsp {

// The environment variable's value, or nothing when it is unset or empty.
[[nodiscard]] inline std::optional<std::string> env_value(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    const auto error = _dupenv_s(&value, nullptr, name);
    const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    if (error != 0 || value == nullptr || *value == '\0') return std::nullopt;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
#endif
    return std::string(value);
}

// The environment variable's value as a path, or nothing when it is unset or
// empty. Windows keeps the value as wide text, so a user name outside the ANSI
// code page survives.
[[nodiscard]] inline std::optional<std::filesystem::path> env_path(const char* name) {
#ifdef _WIN32
    std::wstring wide_name;
    for (const char* letter = name; *letter != '\0'; ++letter) wide_name += static_cast<wchar_t>(*letter);
    wchar_t* value = nullptr;
    const auto error = _wdupenv_s(&value, nullptr, wide_name.c_str());
    const std::unique_ptr<wchar_t, decltype(&std::free)> owned(value, &std::free);
    if (error != 0 || value == nullptr || *value == L'\0') return std::nullopt;
    return std::filesystem::path(value);
#else
    if (const auto value = env_value(name)) return std::filesystem::path(*value);
    return std::nullopt;
#endif
}

}  // namespace lsp
