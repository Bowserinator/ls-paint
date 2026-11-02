#pragma once

// A key/value store for settings, the keys the application uses, and where the
// store lives by default:
//   Linux    $XDG_CONFIG_HOME/ls_paint/config.sqlite3
//            (falls back to ~/.config/ls_paint/config.sqlite3)
//   macOS    ~/Library/Application Support/ls_paint/config.sqlite3
//   Windows  %APPDATA%/ls_paint/config.sqlite3

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "utils/env.hpp"
#include "utils/text.hpp"

namespace lsp {

inline constexpr std::string_view kStoreFilename = "config.sqlite3";

inline constexpr std::string_view kStoreKeyWindowX = "window_x";
inline constexpr std::string_view kStoreKeyWindowY = "window_y";
inline constexpr std::string_view kStoreKeyWindowWidth = "window_width";
inline constexpr std::string_view kStoreKeyWindowHeight = "window_height";
inline constexpr std::string_view kStoreKeyShowRulers = "show_rulers";
inline constexpr std::string_view kStoreKeyShowGridlines = "show_gridlines";
inline constexpr std::string_view kStoreKeyShowStatusBar = "show_status_bar";
inline constexpr std::string_view kStoreKeyShowRibbon = "show_ribbon";
inline constexpr std::string_view kStoreKeyFontFamily = "font_family";
inline constexpr std::string_view kStoreKeyRecentFiles = "recent_files";
inline constexpr std::string_view kStoreKeyPaletteCustoms = "palette_customs";
inline constexpr std::string_view kStoreKeyDialogCustoms = "dialog_customs";
inline constexpr std::string_view kStoreKeyLastDirectory = "last_directory";
inline constexpr std::string_view kStoreKeyBrushKind = "brush_kind";

class Store {
public:
    virtual ~Store() = default;

    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) const = 0;
    virtual void set(std::string_view key, std::string_view value) = 0;
    virtual bool erase(std::string_view key) = 0;
    virtual void load() = 0;
    virtual void save() const = 0;

    // True if the store found its data corrupted on open and started over, so
    // the settings were reset.
    [[nodiscard]] virtual bool was_recovered() const noexcept { return false; }

    [[nodiscard]] virtual std::filesystem::path recovery_backup_path() const { return {}; }
};

// Per-user config directory for this app.
inline std::filesystem::path default_config_dir() {
#if defined(_WIN32)
    if (const auto appdata = env_path("APPDATA")) return *appdata / kAppId;
    if (const auto profile = env_path("USERPROFILE"))
        return *profile / "AppData" / "Roaming" / kAppId;
#elif defined(__APPLE__)
    if (const auto home = env_value("HOME"))
        return std::filesystem::path(*home) / "Library" / "Application Support" / kAppId;
#else
    if (const auto xdg = env_value("XDG_CONFIG_HOME")) return std::filesystem::path(*xdg) / kAppId;
    if (const auto home = env_value("HOME")) return std::filesystem::path(*home) / ".config" / kAppId;
#endif
    return std::filesystem::current_path() / ("." + std::string(kAppId));
}

inline std::filesystem::path default_store_path() {
    return default_config_dir() / kStoreFilename;
}

// Encode a list into a single store value. Items are joined with '|' and each
// one is wrapped in '"'; '\', '|' and '"' inside an item are escaped with a
// backslash. The quotes are what tell a list of one empty item from an empty
// list, which would otherwise both encode to nothing.
inline std::string encode_list(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out.push_back('|');
        out.push_back('"');
        for (char c : items[i]) {
            if (c == '\\' || c == '|' || c == '"') out.push_back('\\');
            out.push_back(c);
        }
        out.push_back('"');
    }
    return out;
}

// Drops the wrapping quotes encode_list writes. Values written before the
// quotes existed came bare and are returned as they are.
inline std::string unwrap_list_item(std::string item) {
    if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
        return item.substr(1, item.size() - 2);
    return item;
}

inline std::vector<std::string> decode_list(std::string_view encoded) {
    std::vector<std::string> items;
    if (encoded.empty()) return items;
    std::string cur;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '\\' && i + 1 < encoded.size()) {
            cur.push_back(encoded[++i]);
            continue;
        }
        if (encoded[i] == '|') {
            items.push_back(unwrap_list_item(std::move(cur)));
            cur.clear();
            continue;
        }
        cur.push_back(encoded[i]);
    }
    items.push_back(unwrap_list_item(std::move(cur)));
    return items;
}

// A stored decimal, or nothing when the key is missing or its text is not a
// whole number.
inline std::optional<int> stored_int(const Store& store, std::string_view key) {
    const std::optional<std::string> text = store.get(key);
    return text ? parse_int(*text) : std::nullopt;
}

// A stored flag: a value of zero is false and everything else is true.
inline std::optional<bool> stored_bool(const Store& store, std::string_view key) {
    const std::optional<int> value = stored_int(store, key);
    return value ? std::optional<bool>(*value != 0) : std::nullopt;
}

}  // namespace lsp
