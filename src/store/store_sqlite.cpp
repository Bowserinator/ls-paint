#include "store/store_sqlite.hpp"

#include <chrono>
#include <cstdio>
#include <sqlite3.h>
#include <system_error>

namespace lsp {

namespace {

constexpr const char* kCREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS kv ("
                                          "  key   TEXT PRIMARY KEY,"
                                          "  value TEXT NOT NULL"
                                          ");";

}  // namespace

SQLiteStore::SQLiteStore(std::filesystem::path path) : m_path(std::move(path)) {
    open_db();
    load();
}

SQLiteStore::~SQLiteStore() {
    if (m_db)
        sqlite3_close(m_db);
}

void SQLiteStore::open_db() {
    std::error_code ec;
    const auto parent = m_path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);

    if (sqlite3_open(m_path.string().c_str(), &m_db) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to open '%s': %s\n", m_path.string().c_str(),
                     m_db ? sqlite3_errmsg(m_db) : "unknown error");
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return;
    }

    // Config is a small, single-writer local file: WAL means a crash mid-save
    // can't corrupt data that was already committed, and NORMAL sync is
    // plenty safe for settings that aren't relied on to survive a power loss.
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // A file can pass sqlite3_open() (which mostly just validates the
    // header) while its data pages are corrupted -- e.g. a crash that
    // truncated the file mid-write, disk bit-rot, or a hand-edited/garbage
    // file that happens to still look like a database. Left unchecked, that
    // corruption doesn't surface until some later get()/load()/save() call
    // fails, at which point the caller has already been told the store is
    // healthy() and keeps silently retrying against a file that will never
    // work -- settings just stop persisting, forever, with nothing but a log
    // line to show for it. Rather than leave it that broken, replace the
    // corrupted file with a fresh one so persistence keeps working; the
    // corrupted original is kept alongside it rather than deleted, and
    // was_recovered()/recovery_backup_path() let the caller tell the user
    // their settings were reset instead of that happening silently.
    if (!database_is_healthy()) {
        std::fprintf(stderr, "SQLiteStore: '%s' failed integrity check\n", m_path.string().c_str());
        sqlite3_close(m_db);
        m_db = nullptr;
        recover_from_corruption();
        return;
    }

    char* err = nullptr;
    if (sqlite3_exec(m_db, kCREATE_TABLE_SQL, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to create table: %s\n", err ? err : "unknown error");
        sqlite3_free(err);
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

namespace {

// SQLite names a database's write-ahead log and its shared-memory file after
// the database itself. Moving a corrupted database without them leaves a stale
// pair beside the fresh file, which a later open may replay or reject.
void move_wal_sidecars(const std::filesystem::path& from, const std::filesystem::path& to) {
    for (const char* suffix : {"-wal", "-shm"}) {
        const std::filesystem::path source = from.string() + suffix;
        std::error_code exists_ec;
        if (!std::filesystem::exists(source, exists_ec))
            continue;
        std::error_code rename_ec;
        std::filesystem::rename(source, to.string() + suffix, rename_ec);
        if (!rename_ec)
            continue;
        std::error_code remove_ec;
        std::filesystem::remove(source, remove_ec);
    }
}

}  // namespace

void SQLiteStore::recover_from_corruption() {
    std::error_code ec;
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    auto backup_path = m_path;
    backup_path += ".corrupt-" + std::to_string(stamp);

    std::filesystem::rename(m_path, backup_path, ec);
    if (ec) {
        // Couldn't even move it aside (permissions, read-only fs, etc.) --
        // don't risk clobbering whatever's there. Fall back to the same
        // in-memory-only mode a failed open already uses.
        std::fprintf(stderr, "SQLiteStore: could not move aside corrupted '%s': %s\n", m_path.string().c_str(),
                     ec.message().c_str());
        return;
    }
    std::fprintf(stderr, "SQLiteStore: backed up corrupted '%s' to '%s', starting fresh\n",
                 m_path.string().c_str(), backup_path.string().c_str());

    move_wal_sidecars(m_path, backup_path);

    if (sqlite3_open(m_path.string().c_str(), &m_db) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to create replacement db '%s': %s\n",
                     m_path.string().c_str(), m_db ? sqlite3_errmsg(m_db) : "unknown error");
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        return;
    }
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    char* err = nullptr;
    if (sqlite3_exec(m_db, kCREATE_TABLE_SQL, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to create table in replacement db: %s\n",
                     err ? err : "unknown error");
        sqlite3_free(err);
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    m_recovered   = true;
    m_backup_path = std::move(backup_path);
}

bool SQLiteStore::database_is_healthy() const {
    // quick_check is the cheap, non-exhaustive variant (unlike the full
    // integrity_check it doesn't verify indexes/foreign keys), which is
    // plenty for a single-table local settings file and stays fast even if
    // the file is large. A brand-new/empty database also reports "ok".
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA quick_check(1);", -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        ok                = text && std::string_view(text) == "ok";
    }
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<std::string> SQLiteStore::get(std::string_view key) const {
    auto it = m_data.find(std::string(key));
    if (it == m_data.end())
        return std::nullopt;
    return it->second;
}

void SQLiteStore::set(std::string_view key, std::string_view value) {
    m_data[std::string(key)] = std::string(value);
}

bool SQLiteStore::erase(std::string_view key) {
    return m_data.erase(std::string(key)) > 0;
}

void SQLiteStore::load() {
    if (!m_db)
        return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT key, value FROM kv;", -1, &stmt, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to prepare select: %s\n", sqlite3_errmsg(m_db));
        return;
    }

    // Read into a local map and only adopt it once the whole table is in:
    // save() writes the cache back, so a half-read cache would drop the rest.
    std::map<std::string, std::string> loaded;
    int step_result = SQLITE_DONE;
    while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto* key_text   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const auto* value_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!key_text)
            continue;
        loaded[std::string(key_text)] = value_text ? std::string(value_text) : std::string();
    }

    if (step_result != SQLITE_DONE)
        std::fprintf(stderr, "SQLiteStore: failed to read every row: %s\n", sqlite3_errmsg(m_db));
    else {
        m_data   = std::move(loaded);
        m_loaded = true;
    }

    sqlite3_finalize(stmt);
}

void SQLiteStore::save() const {
    if (!m_db)
        return;

    // Nothing was read and nothing was set: writing would clear the table.
    if (!m_loaded && m_data.empty())
        return;

    char* err = nullptr;
    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to begin transaction: %s\n", err ? err : "unknown error");
        sqlite3_free(err);
        return;
    }

    bool ok = true;

    if (sqlite3_exec(m_db, "DELETE FROM kv;", nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to clear table: %s\n", err ? err : "unknown error");
        sqlite3_free(err);
        ok = false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (ok && sqlite3_prepare_v2(m_db, "INSERT INTO kv (key, value) VALUES (?, ?);", -1, &stmt, nullptr) != SQLITE_OK) {
        std::fprintf(stderr, "SQLiteStore: failed to prepare insert: %s\n", sqlite3_errmsg(m_db));
        ok = false;
    }

    if (ok) {
        for (const auto& [key, value] : m_data) {
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::fprintf(stderr, "SQLiteStore: failed to write key '%s': %s\n", key.c_str(), sqlite3_errmsg(m_db));
                ok = false;
                break;
            }
        }
    }
    if (stmt)
        sqlite3_finalize(stmt);

    sqlite3_exec(m_db, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
}

}  // namespace lsp
