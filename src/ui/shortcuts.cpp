#include "ui/shortcuts.hpp"

#include <array>

#include "utils/key_text.hpp"
#include "utils/text.hpp"

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
    Shortcut{'r', true, false, ShortcutAction::ToggleRulers},
    Shortcut{'g', true, false, ShortcutAction::ToggleGrid},
};

}  // namespace

std::optional<ShortcutAction> lookup_shortcut(std::string_view text, bool ctrl, bool shift, bool alt) {
    // Alt belongs to the window's own gestures, such as the brush size drag, so
    // no entry matches while it is held.
    if (alt) return std::nullopt;

    // Escape and Delete are control characters, which the key mapping below
    // would read as other keys, so they are matched first.
    if (text == kKeyEscape && !ctrl && !shift) return ShortcutAction::Deselect;
    if (text == kKeyDelete && !ctrl && !shift) return ShortcutAction::Delete;

    if (text.size() != 1) return std::nullopt;

    // Ctrl+letter arrives as the letter's control code, 0x60 below the letter.
    constexpr unsigned char kFirstPrintable = 0x20;
    constexpr int kControlCodeOffset = 0x60;
    char key = text.front();
    if (static_cast<unsigned char>(key) < kFirstPrintable) key = static_cast<char>(key + kControlCodeOffset);
    key = to_lower(key);

    for (const Shortcut& shortcut : kShortcuts) {
        if (shortcut.key == key && shortcut.ctrl == ctrl && shortcut.shift == shift)
            return shortcut.action;
    }
    return std::nullopt;
}

}  // namespace lsp
