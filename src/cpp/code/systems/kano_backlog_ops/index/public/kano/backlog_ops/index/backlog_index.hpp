#pragma once

#include "kano/backlog_core/models/models.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <sqlite3.h>

namespace kano::backlog_ops {

struct IndexItem {
    std::string id;
    std::string uid;
    kano::backlog_core::ItemType type;
    std::string title;
    kano::backlog_core::ItemState state;
    std::string path;
    std::string updated;
};

class BacklogIndex {
public:
    struct SyncSequencesResult {
        std::vector<std::string> synced_pairs;
        int max_number_found;
    };

    explicit BacklogIndex(const std::filesystem::path& db_path);
    ~BacklogIndex();

    /**
     * Initialize DB tables if they don't exist.
     */
    void initialize();

    /**
     * Index or update a single item.
     */
    void index_item(const kano::backlog_core::BacklogItem& item);

    /**
     * Remove an item from the index by its ID.
     */
    void remove_item(const std::string& id);

    /**
     * Get the next sequential ID number for a project-type pair.
     * Atomically increments the counter in id_sequences table.
     */
    int get_next_number(const std::string& prefix, const std::string& type_code);

    /**
     * Scan existing item files and sync sequence counters.
     */
    SyncSequencesResult sync_sequences(const std::filesystem::path& product_root);

    /**
     * Lookup item path by ID.
     */
    std::optional<std::filesystem::path> get_path_by_id(const std::string& id);

    /**
     * Lookup item path by UID.
     */
    std::optional<std::filesystem::path> get_path_by_uid(const std::string& uid);

    /**
     * List items with basic filtering.
     */
    std::vector<IndexItem> query_items(
        std::optional<kano::backlog_core::ItemType> type = std::nullopt,
        std::optional<kano::backlog_core::ItemState> state = std::nullopt
    );

private:
    sqlite3* db_ = nullptr;
    std::filesystem::path db_path_;

    void execute(const std::string& sql);
};

// ============================================================
// Index operations (free functions)
// ============================================================

struct BuildIndexResult {
    std::filesystem::path index_path;
    int items_indexed = 0;
    int links_indexed = 0;
    double build_time_ms = 0.0;
};

struct RefreshIndexResult {
    std::filesystem::path index_path;
    int items_added = 0;
    int items_updated = 0;
    int items_removed = 0;
    double refresh_time_ms = 0.0;
};

struct IndexStatusEntry {
    std::string product;
    std::filesystem::path index_path;
    bool exists = false;
    int item_count = 0;
    size_t size_bytes = 0;
    std::string last_modified;
};

struct GetIndexStatusResult {
    std::vector<IndexStatusEntry> indexes;
};

/**
 * Build the SQLite index from all markdown items under product_root.
 * Drops and recreates the items table.
 */
BuildIndexResult build_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    bool force = false
);

/**
 * Refresh the index (MVP = full rebuild).
 */
RefreshIndexResult refresh_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path
);

/**
 * Get index status and statistics.
 */
GetIndexStatusResult get_index_status(
    const std::filesystem::path& product_root,
    const std::optional<std::string>& product_name = std::nullopt
);

} // namespace kano::backlog_ops
