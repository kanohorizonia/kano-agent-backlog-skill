#pragma once

#include "kano/backlog_core/models/models.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace kano::backlog_ops {

inline constexpr int kMetadataIndexSchemaVersion = 3;
inline constexpr int kMetadataSnapshotSchemaVersion = 3;

struct IndexItem {
    std::string id;
    std::string uid;
    std::string product;
    kano::backlog_core::ItemType type;
    std::string title;
    kano::backlog_core::ItemState state;
    std::optional<std::string> priority;
    std::optional<std::string> parent;
    std::optional<std::string> duplicate_of;
    std::string slug;
    std::string source_ref;
    std::string source_hash;
    std::uint64_t source_size = 0;
    std::int64_t source_mtime_ns = 0;
    std::uint64_t estimated_tokens = 0;
    std::string updated;
};

struct MaterializedIndexInput {
    kano::backlog_core::BacklogItem item;
    std::string_view canonical_content;
};

struct IndexQuery {
    std::optional<kano::backlog_core::ItemType> type;
    std::optional<kano::backlog_core::ItemState> state;
    std::optional<std::string> exact_ref;
    std::optional<std::string> text;
    std::size_t limit = 20000;
};

struct IndexDiagnostics {
    bool index_used = false;
    std::string index_status = "missing";
    std::string index_revision;
    std::string canonical_revision;
    std::string product_revision;
    bool fallback_scan = false;
    std::size_t scanned_count = 0;
    std::size_t matched_count = 0;
    double revision_check_ms = 0.0;
    double elapsed_ms = 0.0;
    std::optional<std::string> stale_reason;
    std::string recovery;
};

struct IndexQueryResult {
    std::vector<IndexItem> items;
    IndexDiagnostics diagnostics;
};

struct IndexDoctorResult {
    bool healthy = false;
    IndexDiagnostics diagnostics;
    std::size_t missing_rows = 0;
    std::size_t orphaned_rows = 0;
    std::size_t stale_rows = 0;
    std::size_t source_hash_mismatches = 0;
};

class BacklogIndex {
public:
    struct RebuildMetadataTestHooks {
        std::function<void()> after_change_watch_capture;
    };

    struct QueryMetadataTestHooks {
        std::function<void()> after_change_proof_verification;
    };

    struct SyncSequencesResult {
        std::vector<std::string> synced_pairs;
        int max_number_found;
    };

    explicit BacklogIndex(
        const std::filesystem::path& db_path,
        std::optional<std::string> product_name = std::nullopt,
        std::optional<std::filesystem::path> product_root = std::nullopt
    );
    ~BacklogIndex();

    BacklogIndex(const BacklogIndex&) = delete;
    BacklogIndex& operator=(const BacklogIndex&) = delete;

    /** Initialize the disposable metadata tables and durable ID sequence tables. */
    void initialize();

    /** Index or update one item after its canonical write succeeds. */
    void index_item(const kano::backlog_core::BacklogItem& item);

    /** Stage exact future canonical bytes and leave metadata invalidated until rebuild. */
    void index_materialized_item(
        const kano::backlog_core::BacklogItem& item,
        const std::string& canonical_content
    );
    void index_materialized_items(const std::vector<MaterializedIndexInput>& items);

    /** Remove one item after its canonical source is removed. */
    void remove_item(const std::string& id);
    void remove_items(const std::vector<std::string>& ids);

    int get_next_number(const std::string& prefix, const std::string& type_code);
    int reserve_next_number(
        const std::string& prefix,
        const std::string& type_code,
        const std::string& owner
    );
    void commit_reservation(const std::string& prefix, const std::string& type_code, int number);
    std::vector<std::string> stale_reservation_diagnostics(
        const std::string& prefix,
        const std::string& type_code,
        int minimum_age_seconds
    );
    bool has_sequence(const std::string& prefix, const std::string& type_code);
    void ensure_sequence_at_least(const std::string& prefix, const std::string& type_code, int number);
    SyncSequencesResult sync_sequences(const std::filesystem::path& product_root);

    std::optional<std::filesystem::path> get_path_by_id(const std::string& id);
    std::optional<std::filesystem::path> get_path_by_uid(const std::string& uid);

    std::vector<IndexItem> query_items(
        std::optional<kano::backlog_core::ItemType> type = std::nullopt,
        std::optional<kano::backlog_core::ItemState> state = std::nullopt,
        std::optional<std::string> product = std::nullopt
    );

    IndexQueryResult query_metadata(
        const std::filesystem::path& product_root,
        const std::string& product,
        const IndexQuery& query = {},
        const QueryMetadataTestHooks& test_hooks = {}
    );

    IndexDoctorResult doctor_metadata(
        const std::filesystem::path& product_root,
        const std::string& product,
        bool verify_source_hashes
    );

    void rebuild_metadata(
        const std::filesystem::path& product_root,
        const std::string& product,
        const RebuildMetadataTestHooks& test_hooks = {}
    );

    void invalidate_metadata(const std::string& product, const std::string& reason);

private:
    sqlite3* db_ = nullptr;
    bool initialized_ = false;
    std::filesystem::path db_path_;
    std::optional<std::string> product_name_;
    std::optional<std::filesystem::path> product_root_;

    void execute(const std::string& sql);
};

struct BuildIndexResult {
    std::filesystem::path index_path;
    std::string index_ref = "product-cache/index/backlog.db";
    int items_indexed = 0;
    int links_indexed = 0;
    double build_time_ms = 0.0;
    std::string index_revision;
    std::string canonical_revision;
    std::string product_revision;
};

struct RefreshIndexResult {
    std::filesystem::path index_path;
    std::string index_ref = "product-cache/index/backlog.db";
    int items_added = 0;
    int items_updated = 0;
    int items_removed = 0;
    double refresh_time_ms = 0.0;
    std::string index_revision;
    std::string canonical_revision;
    std::string product_revision;
};

struct GetIndexStatusTestHooks {
    std::size_t checkpoint_record_cap = 8192;
    std::size_t checkpoint_byte_cap = 1U * 1024U * 1024U;
    std::uint64_t checkpoint_usn_span_cap = 1U * 1024U * 1024U;
    std::optional<std::size_t> proof_records_read_override;
    std::optional<std::size_t> proof_bytes_read_override;
    std::optional<std::uint64_t> proof_usn_span_override;
    std::function<void()> after_revision_state_read;
};

struct IndexStatusEntry {
    std::string product;
    std::string index_ref = "product-cache/index/backlog.db";
    bool exists = false;
    int item_count = 0;
    size_t size_bytes = 0;
    std::string last_modified;
    int schema_version = kMetadataIndexSchemaVersion;
    int snapshot_schema_version = kMetadataSnapshotSchemaVersion;
    std::string status = "missing";
    std::string index_revision;
    std::string canonical_revision;
    std::string product_revision;
    std::optional<std::string> requested_revision;
    bool unchanged = false;
    bool fallback_scan = false;
    std::size_t scanned_count = 0;
    std::size_t proof_records_read = 0;
    std::size_t proof_bytes_read = 0;
    std::uint64_t proof_usn_span = 0;
    bool proof_checkpoint_required = false;
    bool proof_checkpoint_persisted = false;
    double revision_check_ms = 0.0;
    double elapsed_ms = 0.0;
    std::optional<std::string> stale_reason;
    std::string recovery;
};

struct GetIndexStatusResult {
    std::vector<IndexStatusEntry> indexes;
};

BuildIndexResult build_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    bool force = false,
    std::optional<std::string> product_name = std::nullopt
);

RefreshIndexResult refresh_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    std::optional<std::string> product_name = std::nullopt
);

GetIndexStatusResult get_index_status(
    const std::filesystem::path& backlog_root,
    const std::optional<std::string>& product_name = std::nullopt,
    const std::optional<std::filesystem::path>& product_root = std::nullopt,
    const std::optional<std::string>& requested_revision = std::nullopt,
    const GetIndexStatusTestHooks& test_hooks = {}
);

IndexQueryResult query_metadata_index(
    const std::filesystem::path& index_path,
    const std::filesystem::path& product_root,
    const std::string& product,
    const IndexQuery& query = {},
    const BacklogIndex::QueryMetadataTestHooks& test_hooks = {}
);

IndexDoctorResult doctor_metadata_index(
    const std::filesystem::path& index_path,
    const std::filesystem::path& product_root,
    const std::string& product,
    bool verify_source_hashes = true
);

} // namespace kano::backlog_ops
