#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include <map>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <chrono>
#include <fstream>
#include <limits>
#include <utility>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

std::string sqlite_error(sqlite3* db) {
    const char* message = sqlite3_errmsg(db);
    return message ? std::string(message) : std::string("unknown SQLite error");
}

[[noreturn]] void throw_sqlite(sqlite3* db, const std::string& context, int rc) {
    throw std::runtime_error(
        context + " failed (sqlite rc=" + std::to_string(rc) + "): " + sqlite_error(db));
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql, std::string context)
        : db_(db), context_(std::move(context)) {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " prepare", rc);
        }
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const {
        return stmt_;
    }

    void bind_text(int index, const std::string& value) const {
        if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error(context_ + " bind parameter " + std::to_string(index) + " is too large");
        }
        const int rc = sqlite3_bind_text(
            stmt_,
            index,
            value.c_str(),
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind parameter " + std::to_string(index), rc);
        }
    }

    void bind_int(int index, int value) const {
        const int rc = sqlite3_bind_int(stmt_, index, value);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind parameter " + std::to_string(index), rc);
        }
    }

    int step() const {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            throw_sqlite(db_, context_ + " step", rc);
        }
        return rc;
    }

    void step_done() const {
        const int rc = step();
        if (rc != SQLITE_DONE) {
            throw std::runtime_error(context_ + " returned rows unexpectedly");
        }
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
    std::string context_;
};

std::string column_text(sqlite3_stmt* stmt, int column) {
    const int bytes = sqlite3_column_bytes(stmt, column);
    if (bytes <= 0) {
        return {};
    }
    const unsigned char* text = sqlite3_column_text(stmt, column);
    if (!text) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
}

} // namespace

BuildIndexResult build_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    bool force
) {
    BuildIndexResult result;
    result.index_path = index_path;

    auto start = std::chrono::high_resolution_clock::now();

    // Create index
    std::filesystem::create_directories(index_path.parent_path());
    BacklogIndex index(index_path);
    index.initialize();

    // Scan and index all items
    CanonicalStore store(product_root);
    auto item_paths = store.list_items();

    for (const auto& item_path : item_paths) {
        try {
            auto item = store.read(item_path);
            index.index_item(item);
            result.items_indexed++;
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to index " << item_path << ": " << e.what() << "\n";
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.build_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

RefreshIndexResult refresh_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path
) {
    RefreshIndexResult result;
    result.index_path = index_path;

    auto start = std::chrono::high_resolution_clock::now();

    // For MVP, refresh = full rebuild
    auto build_result = build_index(product_root, index_path, true);
    result.items_added = build_result.items_indexed;
    result.items_updated = 0;
    result.items_removed = 0;

    auto end = std::chrono::high_resolution_clock::now();
    result.refresh_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

GetIndexStatusResult get_index_status(
    const std::filesystem::path& product_root,
    const std::optional<std::string>& product_name
) {
    GetIndexStatusResult result;

    std::filesystem::path cache_root = product_root / ".cache" / "index";
    std::filesystem::path index_path = cache_root / "backlog.db";

    IndexStatusEntry entry;
    entry.product = product_name.value_or("default");
    entry.index_path = index_path;

    if (std::filesystem::exists(index_path)) {
        entry.exists = true;
        entry.size_bytes = std::filesystem::file_size(index_path);

        // Get item count
        BacklogIndex idx(index_path);
        auto items = idx.query_items();
        entry.item_count = static_cast<int>(items.size());

        // Get last modified
        auto mod_time = std::filesystem::last_write_time(index_path);
        auto epoch_time = mod_time.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch_time).count();
        entry.last_modified = std::to_string(seconds);
    }

    result.indexes.push_back(entry);
    return result;
}

BacklogIndex::BacklogIndex(const std::filesystem::path& db_path) : db_path_(db_path) {
    std::filesystem::create_directories(db_path.parent_path());
    int rc = sqlite3_open(db_path.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to open index DB: " + std::string(sqlite3_errmsg(db_)));
    }
    rc = sqlite3_busy_timeout(db_, 5000);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to configure bounded index DB lock wait: " + std::string(sqlite3_errmsg(db_)));
    }
}

BacklogIndex::~BacklogIndex() {
    if (db_) sqlite3_close(db_);
}

void BacklogIndex::initialize() {
    execute(
        "CREATE TABLE IF NOT EXISTS items ("
        "  id TEXT PRIMARY KEY,"
        "  uid TEXT UNIQUE NOT NULL,"
        "  type TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  state TEXT NOT NULL,"
        "  duplicate_of TEXT,"
        "  path TEXT NOT NULL,"
        "  updated TEXT NOT NULL"
        ")"
    );

    bool has_duplicate_of = false;
    {
        Statement stmt(db_, "PRAGMA table_info(items)", "inspect items columns");
        while (stmt.step() == SQLITE_ROW) {
            if (column_text(stmt.get(), 1) == "duplicate_of") {
                has_duplicate_of = true;
                break;
            }
        }
    }
    if (!has_duplicate_of) {
        execute("ALTER TABLE items ADD COLUMN duplicate_of TEXT");
    }

    execute(
        "CREATE TABLE IF NOT EXISTS id_sequences ("
        "  prefix TEXT NOT NULL,"
        "  type_code TEXT NOT NULL,"
        "  next_number INTEGER NOT NULL DEFAULT 1,"
        "  PRIMARY KEY (prefix, type_code)"
        ")"
    );
}

void BacklogIndex::index_item(const BacklogItem& item) {
    initialize();
    const char* sql = "INSERT INTO items (id, uid, type, title, state, duplicate_of, path, updated) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(id) DO UPDATE SET "
                      "uid=excluded.uid, type=excluded.type, title=excluded.title, "
                      "state=excluded.state, duplicate_of=excluded.duplicate_of, path=excluded.path, updated=excluded.updated";

    const std::string type_name = to_string(item.type);
    const std::string state_name = to_string(item.state);
    const std::string path_string = item.file_path ? item.file_path->string() : std::string();

    Statement stmt(db_, sql, "index item");
    stmt.bind_text(1, item.id);
    stmt.bind_text(2, item.uid);
    stmt.bind_text(3, type_name);
    stmt.bind_text(4, item.title);
    stmt.bind_text(5, state_name);
    if (item.duplicate_of && !item.duplicate_of->empty()) {
        stmt.bind_text(6, *item.duplicate_of);
    } else {
        const int rc = sqlite3_bind_null(stmt.get(), 6);
        if (rc != SQLITE_OK) throw_sqlite(db_, "index item bind duplicate_of", rc);
    }
    stmt.bind_text(7, path_string);
    stmt.bind_text(8, item.updated);
    stmt.step_done();
}

void BacklogIndex::remove_item(const std::string& id) {
    initialize();
    const char* sql = "DELETE FROM items WHERE id = ?";
    Statement stmt(db_, sql, "remove index item");
    stmt.bind_text(1, id);
    stmt.step_done();
}

int BacklogIndex::get_next_number(const std::string& prefix, const std::string& type_code) {
    initialize();
    execute("BEGIN IMMEDIATE");

    try {
        const char* insert_sql = "INSERT INTO id_sequences (prefix, type_code, next_number) "
                                 "VALUES (?, ?, 1) "
                                 "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = next_number + 1";

        {
            Statement insert_stmt(db_, insert_sql, "advance id sequence");
            insert_stmt.bind_text(1, prefix);
            insert_stmt.bind_text(2, type_code);
            insert_stmt.step_done();
        }

        const char* select_sql = "SELECT next_number FROM id_sequences WHERE prefix = ? AND type_code = ?";
        int number = 0;
        {
            Statement select_stmt(db_, select_sql, "read id sequence");
            select_stmt.bind_text(1, prefix);
            select_stmt.bind_text(2, type_code);

            if (select_stmt.step() != SQLITE_ROW) {
                throw std::runtime_error("read id sequence returned no row for " + prefix + "-" + type_code);
            }
            number = sqlite3_column_int(select_stmt.get(), 0);
        }

        execute("COMMIT");
        return number;
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

bool BacklogIndex::has_sequence(const std::string& prefix, const std::string& type_code) {
    initialize();
    const char* sql = "SELECT 1 FROM id_sequences WHERE prefix = ? AND type_code = ? LIMIT 1";
    Statement stmt(db_, sql, "check id sequence");
    stmt.bind_text(1, prefix);
    stmt.bind_text(2, type_code);
    return stmt.step() == SQLITE_ROW;
}

void BacklogIndex::ensure_sequence_at_least(
    const std::string& prefix,
    const std::string& type_code,
    int number
) {
    initialize();
    execute("BEGIN IMMEDIATE");
    try {
        const char* sql =
            "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES (?, ?, ?) "
            "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = MAX(next_number, excluded.next_number)";
        Statement stmt(db_, sql, "repair id sequence floor");
        stmt.bind_text(1, prefix);
        stmt.bind_text(2, type_code);
        stmt.bind_int(3, number);
        stmt.step_done();
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

BacklogIndex::SyncSequencesResult BacklogIndex::sync_sequences(const std::filesystem::path& product_root) {
    SyncSequencesResult result;
    result.max_number_found = 0;

    std::map<std::pair<std::string, std::string>, int> max_numbers;

    CanonicalStore store(product_root);
    auto item_paths = store.list_items();

    for (const auto& item_path : item_paths) {
        try {
            auto item = store.read(item_path);
            const std::string& id = item.id;
            size_t last_dash = id.rfind('-');
            if (last_dash == std::string::npos) {
                continue;
            }

            std::string seq_str = id.substr(last_dash + 1);
            std::string rest = id.substr(0, last_dash);
            size_t second_dash = rest.rfind('-');
            if (second_dash == std::string::npos) {
                continue;
            }

            std::string type_code = rest.substr(second_dash + 1);
            std::string prefix = rest.substr(0, second_dash);

            try {
                int num = std::stoi(seq_str);
                auto key = std::make_pair(prefix, type_code);
                auto it = max_numbers.find(key);
                if (it == max_numbers.end() || num > it->second) {
                    max_numbers[key] = num;
                }
                if (num > result.max_number_found) {
                    result.max_number_found = num;
                }
            } catch (...) {
            }
        } catch (...) {
        }
    }

    for (const auto& [key, max_num] : max_numbers) {
        const std::string& prefix = key.first;
        const std::string& type_code = key.second;

        std::string upsert_sql =
            "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES ('" + prefix + "', '" + type_code + "', " + std::to_string(max_num) + ") "
            "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = MAX(next_number, " + std::to_string(max_num) + ")";
        execute(upsert_sql);

        result.synced_pairs.push_back(prefix + "-" + type_code + " -> " + std::to_string(max_num + 1));
    }

    return result;
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_id(const std::string& id) {
    initialize();
    const char* sql = "SELECT path FROM items WHERE id = ?";
    Statement stmt(db_, sql, "lookup item path by id");
    stmt.bind_text(1, id);

    std::optional<std::filesystem::path> res;
    if (stmt.step() == SQLITE_ROW) {
        res = column_text(stmt.get(), 0);
    }
    return res;
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_uid(const std::string& uid) {
    initialize();
    const char* sql = "SELECT path FROM items WHERE uid = ?";
    Statement stmt(db_, sql, "lookup item path by uid");
    stmt.bind_text(1, uid);

    std::optional<std::filesystem::path> res;
    if (stmt.step() == SQLITE_ROW) {
        res = column_text(stmt.get(), 0);
    }
    return res;
}

void BacklogIndex::execute(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg;
        sqlite3_free(errmsg);
        throw std::runtime_error("SQL error: " + err);
    }
}

std::vector<IndexItem> BacklogIndex::query_items(std::optional<ItemType> type, std::optional<ItemState> state) {
    initialize();
    std::vector<IndexItem> results;
    std::string sql = "SELECT id, uid, type, title, state, duplicate_of, path, updated FROM items";
    std::vector<std::string> params;

    bool has_where = false;
    if (type) {
        sql += " WHERE type = ?";
        params.push_back(to_string(*type));
        has_where = true;
    }
    if (state) {
        sql += has_where ? " AND state = ?" : " WHERE state = ?";
        params.push_back(to_string(*state));
    }
    sql += " ORDER BY updated DESC";

    Statement stmt(db_, sql.c_str(), "query index items");
    for (size_t i = 0; i < params.size(); ++i) {
        stmt.bind_text(static_cast<int>(i + 1), params[i]);
    }

    while (stmt.step() == SQLITE_ROW) {
        IndexItem ii;
        ii.id = column_text(stmt.get(), 0);
        ii.uid = column_text(stmt.get(), 1);

        const std::string type_value = column_text(stmt.get(), 2);
        const std::string state_value = column_text(stmt.get(), 4);

        auto type_opt = parse_item_type(type_value);
        if (!type_opt) {
            std::cerr << "Warning: Invalid item type '" << type_value << "' for item " << ii.id << "\n";
            continue;
        }
        ii.type = *type_opt;

        ii.title = column_text(stmt.get(), 3);

        auto state_opt = parse_item_state(state_value);
        if (!state_opt) {
            std::cerr << "Warning: Invalid item state '" << state_value << "' for item " << ii.id << "\n";
            continue;
        }
        ii.state = *state_opt;

        const auto duplicate_of = column_text(stmt.get(), 5);
        if (!duplicate_of.empty()) ii.duplicate_of = duplicate_of;
        ii.path = column_text(stmt.get(), 6);
        ii.updated = column_text(stmt.get(), 7);
        results.push_back(ii);
    }
    return results;
}

} // namespace kano::backlog_ops
