#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "store/store.hpp"
#include "store/store_sqlite.hpp"

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

void test_a_value_survives_a_reopen() {
    const std::filesystem::path path = fresh_directory("lsp_store_round_trip") / "config.sqlite3";
    {
        SQLiteStore store(path);
        CHECK(store.healthy());
        CHECK(!store.get(kSTORE_KEY_WINDOW_WIDTH).has_value());
        store.set(kSTORE_KEY_WINDOW_WIDTH, "1024");
        store.set(kSTORE_KEY_FONT_FAMILY, "DejaVu Serif");
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(reopened.healthy());
    CHECK(!reopened.was_recovered());
    const std::optional<std::string> width = reopened.get(kSTORE_KEY_WINDOW_WIDTH);
    CHECK(width.has_value() && *width == "1024");
    const std::optional<std::string> family = reopened.get(kSTORE_KEY_FONT_FAMILY);
    CHECK(family.has_value() && *family == "DejaVu Serif");
}

void test_erase_removes_a_key() {
    const std::filesystem::path path = fresh_directory("lsp_store_erase") / "config.sqlite3";
    {
        SQLiteStore store(path);
        store.set(kSTORE_KEY_SHOW_RULERS, "1");
        store.set(kSTORE_KEY_SHOW_GRIDLINES, "1");
        store.save();
    }

    {
        SQLiteStore store(path);
        CHECK(store.erase(kSTORE_KEY_SHOW_RULERS));
        CHECK(!store.erase("never_set"));
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(!reopened.get(kSTORE_KEY_SHOW_RULERS).has_value());
    const std::optional<std::string> grid = reopened.get(kSTORE_KEY_SHOW_GRIDLINES);
    CHECK(grid.has_value() && *grid == "1");
}

void test_a_large_value_survives_a_reopen() {
    const std::filesystem::path path = fresh_directory("lsp_store_large") / "config.sqlite3";
    const std::string long_value(kSTORE_KEY_RECENT_FILES.size() * 40, 'x');
    {
        SQLiteStore store(path);
        store.set(kSTORE_KEY_RECENT_FILES, "first|second|third");
        store.set("long_value", long_value);
        store.save();
    }

    SQLiteStore reopened(path);
    const std::optional<std::string> stored = reopened.get("long_value");
    CHECK(stored.has_value() && *stored == long_value);
    const std::optional<std::string> files = reopened.get(kSTORE_KEY_RECENT_FILES);
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
        CHECK(!store.get(kSTORE_KEY_WINDOW_WIDTH).has_value());
        store.set(kSTORE_KEY_WINDOW_WIDTH, "800");
        store.save();
    }

    SQLiteStore reopened(path);
    CHECK(!reopened.was_recovered());
    CHECK(reopened.get(kSTORE_KEY_WINDOW_WIDTH).value_or("") == "800");

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

void test_the_default_path_names_the_app_directory() {
    const std::filesystem::path path = default_store_path();
    CHECK(path.filename() == kSTORE_FILENAME);
    CHECK(path.parent_path().filename() == kAPP_ID);
    // The base is the platform's per-user config directory, so XDG_CONFIG_HOME
    // decides it wherever that variable is set.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
        CHECK(path.parent_path().parent_path() == std::filesystem::path(xdg));
}

}  // namespace

void test_a_store_that_never_loaded_does_not_clear_the_table() {
    const std::filesystem::path path = fresh_directory("lsp_store_noload") / "config.sqlite3";
    {
        SQLiteStore seeded(path);
        seeded.set(kSTORE_KEY_WINDOW_WIDTH, "800");
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
    CHECK(reread.get(kSTORE_KEY_WINDOW_WIDTH) == std::optional<std::string>("800"));
}

int main() {
    test_a_value_survives_a_reopen();
    test_erase_removes_a_key();
    test_a_large_value_survives_a_reopen();
    test_an_unreadable_database_is_backed_up_and_replaced();
    test_a_store_that_never_loaded_does_not_clear_the_table();
    test_a_list_round_trips_through_encoding();
    test_the_default_path_names_the_app_directory();

    if (failures == 0) {
        std::printf("ALL OK\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
