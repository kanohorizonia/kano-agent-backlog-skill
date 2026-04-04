#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include <map>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <chrono>
#include <fstream>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

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
        "  path TEXT NOT NULL,"
        "  updated TEXT NOT NULL"
        ")"
    );

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
    const char* sql = "INSERT INTO items (id, uid, type, title, state, path, updated) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(id) DO UPDATE SET "
                      "uid=excluded.uid, type=excluded.type, title=excluded.title, "
                      "state=excluded.state, path=excluded.path, updated=excluded.updated";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    
    sqlite3_bind_text(stmt, 1, item.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, item.uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, to_string(item.type).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, to_string(item.state).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, item.file_path ? item.file_path->string().c_str() : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, item.updated.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void BacklogIndex::remove_item(const std::string& id) {
    const char* sql = "DELETE FROM items WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int BacklogIndex::get_next_number(const std::string& prefix, const std::string& type_code) {
    execute("BEGIN IMMEDIATE");
    
    const char* insert_sql = "INSERT INTO id_sequences (prefix, type_code, next_number) "
                             "VALUES (?, ?, 1) "
                             "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = next_number + 1";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    const char* select_sql = "SELECT next_number FROM id_sequences WHERE prefix = ? AND type_code = ?";
    sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, type_code.c_str(), -1, SQLITE_TRANSIENT);
    
    int number = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        number = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    execute("COMMIT");
    return number;
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
            "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES ('" + prefix + "', '" + type_code + "', " + std::to_string(max_num + 1) + ") "
            "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = MAX(next_number, " + std::to_string(max_num + 1) + ")";
        execute(upsert_sql);

        result.synced_pairs.push_back(prefix + "-" + type_code + " -> " + std::to_string(max_num + 1));
    }

    return result;
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_id(const std::string& id) {
    const char* sql = "SELECT path FROM items WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    std::optional<std::filesystem::path> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        res = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return res;
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_uid(const std::string& uid) {
    const char* sql = "SELECT path FROM items WHERE uid = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    
    std::optional<std::filesystem::path> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        res = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
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
    std::vector<IndexItem> results;
    std::stringstream ss;
    ss << "SELECT id, uid, type, title, state, path, updated FROM items";
    
    bool has_where = false;
    if (type) {
        ss << " WHERE type = '" << to_string(*type) << "'";
        has_where = true;
    }
    if (state) {
        if (has_where) ss << " AND"; else ss << " WHERE";
        ss << " state = '" << to_string(*state) << "'";
    }
    ss << " ORDER BY updated DESC";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, ss.str().c_str(), -1, &stmt, nullptr);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IndexItem ii;
        auto id_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        auto uid_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto type_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto title_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        auto state_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        auto path_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        auto updated_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

        ii.id = id_ptr ? id_ptr : "";
        ii.uid = uid_ptr ? uid_ptr : "";
        
        auto type_opt = parse_item_type(type_ptr ? type_ptr : "");
        if (!type_opt) {
            std::cerr << "Warning: Invalid item type '" << (type_ptr ? type_ptr : "NULL") << "' for item " << ii.id << "\n";
            continue;
        }
        ii.type = *type_opt;

        ii.title = title_ptr ? title_ptr : "";
        
        auto state_opt = parse_item_state(state_ptr ? state_ptr : "");
        if (!state_opt) {
            std::cerr << "Warning: Invalid item state '" << (state_ptr ? state_ptr : "NULL") << "' for item " << ii.id << "\n";
            continue;
        }
        ii.state = *state_opt;

        ii.path = path_ptr ? path_ptr : "";
        ii.updated = updated_ptr ? updated_ptr : "";
        results.push_back(ii);
    }
    sqlite3_finalize(stmt);
    return results;
}

} // namespace kano::backlog_ops
