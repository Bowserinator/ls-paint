#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace lsp {

// A path as UTF-8, whatever the platform's native encoding is.
[[nodiscard]] inline std::string utf8_from_path(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

[[nodiscard]] inline std::filesystem::path path_from_utf8(std::string_view text) {
    const std::u8string bytes(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return std::filesystem::path(bytes);
}

}  // namespace lsp
