#pragma once

// A Store backed by a single-table SQLite database.
//
// get/set/erase work on an in-memory cache; load() replaces the cache with the
// table's contents and save() writes the cache back in one transaction.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "store.hpp"

// Forward-declared so headers that only need the Store interface do not pull
// in the C API.
struct sqlite3;

namespace lsp {

class SQLiteStore : public Store {
public:
    explicit SQLiteStore(std::filesystem::path path);
    ~SQLiteStore() override;

    SQLiteStore(const SQLiteStore&) = delete;
    SQLiteStore& operator=(const SQLiteStore&) = delete;
    SQLiteStore(SQLiteStore&&) = delete;
    SQLiteStore& operator=(SQLiteStore&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    // False when the database could not be opened. The store still works in
    // memory, but nothing persists across runs.
    [[nodiscard]] bool healthy() const noexcept { return m_db != nullptr; }

    [[nodiscard]] bool was_recovered() const noexcept override { return m_recovered; }
    [[nodiscard]] std::filesystem::path recovery_backup_path() const override { return m_backup_path; }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const override;
    void set(std::string_view key, std::string_view value) override;
    bool erase(std::string_view key) override;
    void load() override;
    void save() const override;

private:
    void open_db();
    // Opens the file and sets its journal mode. Leaves the handle null on
    // failure.
    [[nodiscard]] bool connect();
    // Makes sure the table exists, closing the database when it cannot.
    [[nodiscard]] bool create_table();
    void close_db();
    [[nodiscard]] bool database_is_healthy() const;
    // Moves a corrupted database aside and starts a fresh one in its place.
    void recover_from_corruption();

    std::filesystem::path m_path;
    sqlite3* m_db = nullptr;
    std::map<std::string, std::string> m_data;
    bool m_loaded = false;
    bool m_recovered = false;
    std::filesystem::path m_backup_path;
};

}  // namespace lsp
