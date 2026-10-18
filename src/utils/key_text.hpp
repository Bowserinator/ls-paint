#pragma once

#include <string_view>

namespace lsp {

// The text a key press arrives as, for the keys that are control characters.
inline constexpr std::string_view kKeyEscape = "\x1b";
inline constexpr std::string_view kKeyEnter = "\n";
inline constexpr std::string_view kKeyDelete = "\x7f";

}  // namespace lsp
