#include "ui/shortcuts.hpp"

#include <array>
#include <cctype>

namespace lsp {

namespace {

struct Shortcut {
    char key;
    bool ctrl;
    bool shift;
    ShortcutAction action;
};

constexpr std::array kShortcuts{
    Shortcut{'z', true, false, ShortcutAction::Undo},
    Shortcut{'z', true, true, ShortcutAction::Redo},
    Shortcut{'y', true, false, ShortcutAction::Redo},
    Shortcut{'x', true, false, ShortcutAction::Cut},
    Shortcut{'c', true, false, ShortcutAction::Copy},
    Shortcut{'v', true, false, ShortcutAction::Paste},
    Shortcut{'a', true, false, ShortcutAction::SelectAll},
    Shortcut{'n', true, false, ShortcutAction::New},
    Shortcut{'o', true, false, ShortcutAction::Open},
    Shortcut{'s', true, false, ShortcutAction::Save},
    Shortcut{'s', true, true, ShortcutAction::SaveAs},
};

}  // namespace

std::optional<ShortcutAction> lookup_shortcut(std::string_view text, bool ctrl, bool shift) {
    if (text.size() != 1) return std::nullopt;

    char key = text.front();
    if (static_cast<unsigned char>(key) < 0x20) key = static_cast<char>(key + 0x60);
    key = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));

    for (const Shortcut& shortcut : kShortcuts)
        if (shortcut.key == key && shortcut.ctrl == ctrl && shortcut.shift == shift) return shortcut.action;
    return std::nullopt;
}

}  // namespace lsp
