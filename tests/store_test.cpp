#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "store/store.hpp"
#include "store/store_sqlite.hpp"
#include "store/window_settings.hpp"

using namespace lsp;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

// A directory of its own under the system's temp directory, emptied first.
std::filesystem::path fresh_directory(std::string_view name) {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / name;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

class MemoryStore final : public Store {
public:
    std::map<std::string, std::string> values;
    mutable std::map<std::string, std::string> persisted;
    mutable int saves = 0;

    std::optional<std::string> get(std::string_view key) const override {
        const auto it = values.find(std::string(key));
        return it == values.end() ? std::nullopt : std::optional(it->second);
    }
    void set(std::string_view key, std::string_view value) override { values[std::string(key)] = value; }
    bool erase(std::string_view key) override { return values.erase(std::string(key)) != 0; }
    void load() override { values = persisted; }
    void save() const override { persisted = values; ++saves; }
};

void test_window_settings_are_repaired_through_the_interface() {
    const std::array screens{RectI{0, 0, 1920, 1080}, RectI{-1280, 0, 1280, 1024}};
    const RectI defaults{0, 0, 900, 600};
    struct Case { const char* x; const char* y; const char* width; const char* height; RectI expected; };
    for (const Case& c : {
        Case{"60", "40", "744", "522", {60, 40, 744, 522}},
        Case{"-1200", "40", "1000", "800", {-1200, 40, 1000, 800}},
        Case{"50000", "50000", "900", "600", {1020, 480, 900, 600}},
        Case{"-2147483648", "2147483647", "900", "600", {0, 480, 900, 600}},
        Case{"0", "0", "2147483647", "2147483647", {0, 0, 1920, 1080}},
        Case{"0", "0", "0", "-12", {0, 0, 620, 240}},
        Case{"garbage", "1.5", "999999999999999999999", "600px", defaults}}) {
        MemoryStore memory;
        Store& store = memory;
        store.set(kStoreKeyWindowX, c.x);
        store.set(kStoreKeyWindowY, c.y);
        store.set(kStoreKeyWindowWidth, c.width);
        store.set(kStoreKeyWindowHeight, c.height);
        store.set("unrelated", "Keep me");
        CHECK(restore_window_settings(store, defaults, screens) == c.expected);
        CHECK(stored_int(store, kStoreKeyWindowX) == c.expected.x);
        CHECK(stored_int(store, kStoreKeyWindowY) == c.expected.y);
        CHECK(stored_int(store, kStoreKeyWindowWidth) == c.expected.width);
        CHECK(stored_int(store, kStoreKeyWindowHeight) == c.expected.height);
        CHECK(store.get("unrelated") == "Keep me");
        const int saves = memory.saves;
        CHECK(restore_window_settings(store, defaults, screens) == c.expected);
        CHECK(memory.saves == saves);
        if (saves != 0) {
            store.load();
            CHECK(restore_window_settings(store, defaults, screens) == c.expected);
            CHECK(memory.saves == saves);
        }
    }
    MemoryStore store;
    store.set(kStoreKeyWindowX, "-1200");
    store.set(kStoreKeyWindowY, "40");
    CHECK((restore_window_settings(store, defaults, std::span(screens).first(1)) == RectI{0, 40, 900, 600}));
    const std::array tiny{RectI{0, 0, 320, 200}};
    CHECK((restore_window_settings(store, defaults, tiny) == RectI{0, 0, 320, 200}));
    const std::array separated{RectI{0, 0, 1920, 1080}, RectI{3000, 0, 1920, 1080}};
    store.set(kStoreKeyWindowX, "2100");
    store.set(kStoreKeyWindowWidth, "620");
    store.set(kStoreKeyWindowHeight, "240");
    CHECK((restore_window_settings(store, defaults, separated) == RectI{1300, 0, 620, 240}));
    CHECK(restore_window_settings(store, defaults, {}) == defaults);
    CHECK(!store.get(kStoreKeyWindowX));
    CHECK(!store.get(kStoreKeyWindowY));
    CHECK(restore_window_settings(store, defaults, screens, false) == defaults);
    CHECK(!store.get(kStoreKeyWindowX));
    CHECK(!store.get(kStoreKeyWindowY));
}

void test_repaired_window_settings_survive_a_database_reopen() {
    const auto path = fresh_directory("lsp_store_window_repair") / "config.sqlite3";
    const std::array screens{RectI{0, 0, 1280, 720}};
    {
        SQLiteStore sqlite(path);
        Store& store = sqlite;
        store.set(kStoreKeyWindowX, "2147483647");
        store.set(kStoreKeyWindowY, "-2147483648");
        store.set(kStoreKeyWindowWidth, "2147483647");
        store.set(kStoreKeyWindowHeight, "0");
        CHECK((restore_window_settings(store, {0, 0, 900, 600}, screens) == RectI{0, 0, 1280, 240}));
    }
    SQLiteStore reopened(path);
    CHECK(stored_int(reopened, kStoreKeyWindowX) == 0);
    CHECK(stored_int(reopened, kStoreKeyWindowY) == 0);
    CHECK(stored_int(reopened, kStoreKeyWindowWidth) == 1280);
    CHECK(stored_int(reopened, kStoreKeyWindowHeight) == 240);
}

void test_a_value_survives_a_reopen() {
    const std::filesystem::path path = fresh_directory("lsp_store_round_trip") / "config.sqlite3";
    {
        SQLiteStore store(path);
        CHECK(store.healthy());
        CHECK(!store.get(kStoreKeyWindowWidth).has_value());
        store.set(kStoreKeyWindowWidth, "1024");
        store.set(kStoreKeyFontFamily, "DejaVu Serif");
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(reopened.healthy());
    CHECK(!reopened.was_recovered());
    const std::optional<std::string> width = reopened.get(kStoreKeyWindowWidth);
    CHECK(width.has_value() && *width == "1024");
    const std::optional<std::string> family = reopened.get(kStoreKeyFontFamily);
    CHECK(family.has_value() && *family == "DejaVu Serif");
}

void test_erase_removes_a_key() {
    const std::filesystem::path path = fresh_directory("lsp_store_erase") / "config.sqlite3";
    {
        SQLiteStore store(path);
        store.set(kStoreKeyShowRulers, "1");
        store.set(kStoreKeyShowGridlines, "1");
        store.save();
    }

    {
        SQLiteStore store(path);
        CHECK(store.erase(kStoreKeyShowRulers));
        CHECK(!store.erase("never_set"));
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(!reopened.get(kStoreKeyShowRulers).has_value());
    const std::optional<std::string> grid = reopened.get(kStoreKeyShowGridlines);
    CHECK(grid.has_value() && *grid == "1");
}

void test_a_large_value_survives_a_reopen() {
    const std::filesystem::path path = fresh_directory("lsp_store_large") / "config.sqlite3";
    const std::string long_value(kStoreKeyRecentFiles.size() * 40, 'x');
    {
        SQLiteStore store(path);
        store.set(kStoreKeyRecentFiles, "first|second|third");
        store.set("long_value", long_value);
        store.save();
    }

    SQLiteStore reopened(path);
    const std::optional<std::string> stored = reopened.get("long_value");
    CHECK(stored.has_value() && *stored == long_value);
    const std::optional<std::string> files = reopened.get(kStoreKeyRecentFiles);
    CHECK(files.has_value() && *files == "first|second|third");
    CHECK(decode_list(*files) == std::vector<std::string>({"first", "second", "third"}));
}

void test_an_unreadable_database_is_backed_up_and_replaced() {
    const std::filesystem::path path = fresh_directory("lsp_store_recover") / "config.sqlite3";
    const std::string garbage(4096, '\xAA');
    {
        std::ofstream out(path, std::ios::binary);
        out.write(garbage.data(), static_cast<std::streamsize>(garbage.size()));
    }

    // The file cannot be read, so it is moved aside and a fresh database takes
    // its place: settings keep persisting instead of silently never working.
    std::filesystem::path backup;
    {
        SQLiteStore store(path);
        CHECK(store.was_recovered());
        CHECK(store.healthy());
        backup = store.recovery_backup_path();
        CHECK(!backup.empty());
        CHECK(std::filesystem::exists(backup));
        CHECK(!store.get(kStoreKeyWindowWidth).has_value());
        store.set(kStoreKeyWindowWidth, "800");
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(!reopened.was_recovered());
    CHECK(reopened.get(kStoreKeyWindowWidth).value_or("") == "800");

    // The unreadable original is kept beside the replacement, byte for byte.
    std::string backed_up(garbage.size(), '\0');
    {
        std::ifstream in(backup, std::ios::binary);
        in.read(backed_up.data(), static_cast<std::streamsize>(backed_up.size()));
    }
    CHECK(backed_up == garbage);
}

void test_a_list_round_trips_through_encoding() {
    const std::vector<std::string> items{"/home/me/a|b.png", "C:\\pictures\\c.png", "plain", ""};
    CHECK(decode_list(encode_list(items)) == items);
    // A list of one empty item is not an empty list: the encoder quotes every
    // item so the two differ on the wire.
    CHECK(encode_list({std::string()}) != encode_list({}));
    CHECK(decode_list(encode_list({std::string()})) == std::vector<std::string>({""}));
    CHECK(decode_list(encode_list({std::string("a\"b")})) == std::vector<std::string>({"a\"b"}));
    CHECK(encode_list({}).empty());
    CHECK(decode_list("").empty());
    CHECK(decode_list("one") == std::vector<std::string>({"one"}));
    CHECK(decode_list("a|b") == std::vector<std::string>({"a", "b"}));
    CHECK(decode_list(encode_list({std::string("back\\slash")})) ==
          std::vector<std::string>({"back\\slash"}));
}

void test_environment_values() {
    constexpr const char* name = "LSP_TEST_ENV_VALUE";
    const auto previous = env_value(name);
    const auto set_value = [name](const char* value) {
#ifdef _WIN32
        CHECK(_putenv_s(name, value) == 0);
#else
        CHECK(setenv(name, value, 1) == 0);
#endif
    };

    set_value("");
    CHECK(!env_value(name));
    CHECK(!env_path(name));
    set_value("A path with spaces");
    CHECK(env_value(name) == "A path with spaces");
    CHECK(env_path(name) == std::filesystem::path("A path with spaces"));
    const std::string long_value(8192, 'x');
    set_value(long_value.c_str());
    CHECK(env_value(name) == long_value);
    CHECK(env_path(name) == std::filesystem::path(long_value));
#ifdef _WIN32
    CHECK(_wputenv_s(L"LSP_TEST_ENV_VALUE", L"C:\\Users\\\u6d4b\u8bd5") == 0);
    CHECK(env_path(name) == std::filesystem::path(L"C:\\Users\\\u6d4b\u8bd5"));
    CHECK(_wputenv_s(L"LSP_TEST_ENV_VALUE", L"") == 0);
#else
    CHECK(unsetenv(name) == 0);
#endif
    CHECK(!env_value(name));
    CHECK(!env_path(name));
    if (previous) set_value(previous->c_str());
}

void test_the_default_path_names_the_app_directory() {
    const std::filesystem::path path = default_store_path();
    CHECK(path.filename() == kStoreFilename);
    CHECK(path.parent_path().filename() == kAppId);
    // The base is the platform's per-user config directory, so XDG_CONFIG_HOME
    // decides it wherever that variable is set.
    if (const auto xdg = env_path("XDG_CONFIG_HOME"))
        CHECK(path.parent_path().parent_path() == *xdg);
}

}  // namespace

void test_a_store_that_never_loaded_does_not_clear_the_table() {
    const std::filesystem::path path = fresh_directory("lsp_store_noload") / "config.sqlite3";
    {
        SQLiteStore seeded(path);
        seeded.set(kStoreKeyWindowWidth, "800");
        seeded.save();
    }

    // save() writes the whole cache back, so a store whose load never ran must
    // not be allowed to write an empty cache over the stored keys.
    {
        SQLiteStore untouched(path);
        untouched.save();
    }

    SQLiteStore reread(path);
    reread.load();
    CHECK(reread.get(kStoreKeyWindowWidth) == std::optional<std::string>("800"));
}

int main() {
    test_a_value_survives_a_reopen();
    test_erase_removes_a_key();
    test_a_large_value_survives_a_reopen();
    test_an_unreadable_database_is_backed_up_and_replaced();
    test_a_store_that_never_loaded_does_not_clear_the_table();
    test_a_list_round_trips_through_encoding();
    test_the_default_path_names_the_app_directory();
    test_environment_values();
    test_window_settings_are_repaired_through_the_interface();
    test_repaired_window_settings_survive_a_database_reopen();

    if (failures == 0) {
        std::printf("ALL OK\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
