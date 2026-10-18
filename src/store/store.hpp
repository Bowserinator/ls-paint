#pragma once

// store.hpp
//
// Abstract key/value store interface, shared store keys, and the
// cross-platform config directory helpers. The concrete implementation
// (SQLiteStore, backed by the vendored third_party/sqlite3 amalgamation)
// lives in store_sqlite.hpp/.cpp.
//
// Default location (via default_store_path()):
//   Linux    $XDG_CONFIG_HOME/ls_paint/config.sqlite3
//            (falls back to ~/.config/ls_paint/config.sqlite3)
//   macOS    ~/Library/Application Support/ls_paint/config.sqlite3
//   Windows  %APPDATA%/ls_paint/config.sqlite3

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"

namespace lsp {

inline constexpr std::string_view kSTORE_FILENAME = "config.sqlite3";

inline constexpr std::string_view kSTORE_KEY_WINDOW_X        = "window_x";
inline constexpr std::string_view kSTORE_KEY_WINDOW_Y        = "window_y";
inline constexpr std::string_view kSTORE_KEY_WINDOW_WIDTH    = "window_width";
inline constexpr std::string_view kSTORE_KEY_WINDOW_HEIGHT   = "window_height";
inline constexpr std::string_view kSTORE_KEY_SHOW_RULERS     = "show_rulers";
inline constexpr std::string_view kSTORE_KEY_SHOW_GRIDLINES  = "show_gridlines";
inline constexpr std::string_view kSTORE_KEY_SHOW_STATUS_BAR = "show_status_bar";
inline constexpr std::string_view kSTORE_KEY_FONT_FAMILY     = "font_family";
inline constexpr std::string_view kSTORE_KEY_RECENT_FILES    = "recent_files";
inline constexpr std::string_view kSTORE_KEY_PALETTE_CUSTOMS = "palette_customs";
inline constexpr std::string_view kSTORE_KEY_DIALOG_CUSTOMS  = "dialog_customs";
inline constexpr std::string_view kSTORE_KEY_LAST_DIRECTORY  = "last_directory";

class Store {
public:
    virtual ~Store() = default;

    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) const = 0;
    virtual void set(std::string_view key, std::string_view value)                   = 0;
    virtual bool erase(std::string_view key)                                         = 0;
    virtual void load()                                                              = 0;
    virtual void save() const                                                        = 0;

    // True if this store found its backing data corrupted on open and
    // replaced it with a fresh, empty one (the user's settings were reset).
    // Default false; backends with nothing to recover from need not
    // override it.
    [[nodiscard]] virtual bool was_recovered() const noexcept { return false; }

    [[nodiscard]] virtual std::filesystem::path recovery_backup_path() const { return {}; }
};

// Cross-platform per-user config directory for this app.
inline std::filesystem::path default_config_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::filesystem::path(appdata) / kAPP_ID;
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
        return std::filesystem::path(profile) / "AppData" / "Roaming" / kAPP_ID;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / "Library" / "Application Support" / kAPP_ID;
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / kAPP_ID;
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / kAPP_ID;
#endif
    return std::filesystem::current_path() / ("." + std::string(kAPP_ID));
}

inline std::filesystem::path default_store_path() {
    return default_config_dir() / kSTORE_FILENAME;
}

// Encode a list into a single store value. Items are joined with '|' and each
// one is wrapped in '"'; '\', '|' and '"' inside an item are escaped with a
// backslash. The quotes are what tell a list of one empty item from an empty
// list, which would otherwise both encode to nothing.
inline std::string encode_list(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i)
            out.push_back('|');
        out.push_back('"');
        for (char c : items[i]) {
            if (c == '\\' || c == '|' || c == '"')
                out.push_back('\\');
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
    if (encoded.empty())
        return items;
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

}  // namespace lsp