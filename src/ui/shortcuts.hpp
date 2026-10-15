#pragma once

#include <optional>
#include <string_view>

namespace lsp {

enum class ShortcutAction { Undo, Redo, Cut, Copy, Paste, SelectAll };

[[nodiscard]] std::optional<ShortcutAction> lookup_shortcut(std::string_view text, bool ctrl, bool shift);

}  // namespace lsp
