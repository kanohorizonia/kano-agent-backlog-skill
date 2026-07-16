#pragma once

#include "kano/backlog_core/models/models.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kano::backlog_ops {

inline constexpr const char* kMigrationRequestSchema = "kob.cross_product_migration.request.v1";
inline constexpr const char* kMigrationPlanSchema = "kob.cross_product_migration.plan.v1";
inline constexpr const char* kMigrationResultSchema = "kob.cross_product_migration.result.v1";
inline constexpr const char* kMigrationVerificationSchema = "kob.cross_product_migration.verification.v1";
inline constexpr const char* kMigrationStatusSchema = "kob.cross_product_migration.status.v1";
inline constexpr const char* kMigrationRollbackSchema = "kob.cross_product_migration.rollback.v1";
inline constexpr std::size_t kDefaultMigrationMaxSourceInventoryItems = 100000;
inline constexpr std::size_t kDefaultMigrationMaxTargetInventoryItems = 100000;
inline constexpr std::uintmax_t kDefaultMigrationMaxArtifactBytes = 16ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::uintmax_t kDefaultMigrationMaxMaterializationBytes = 32ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultMigrationMaxReferenceFiles = 500000;
inline constexpr std::uintmax_t kDefaultMigrationMaxReferenceBytes = 8ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultMigrationMaxReferences = 500000;

struct MigrationRequest {
    std::string schema = kMigrationRequestSchema;
    std::string source_product;
    std::string source_ref;
    std::string target_product;
    std::optional<std::string> expected_source_revision;
    std::optional<std::string> expected_target_prefix;
    std::string scope = "subtree";
    bool include_owned_artifacts = true;
    std::size_t max_items = 10000;
    std::size_t max_artifacts = 50000;
    std::size_t max_source_inventory_items = kDefaultMigrationMaxSourceInventoryItems;
    std::size_t max_target_inventory_items = kDefaultMigrationMaxTargetInventoryItems;
    std::uintmax_t max_artifact_bytes = kDefaultMigrationMaxArtifactBytes;
    std::uintmax_t max_materialization_bytes = kDefaultMigrationMaxMaterializationBytes;
    std::size_t max_reference_files = kDefaultMigrationMaxReferenceFiles;
    std::uintmax_t max_reference_bytes = kDefaultMigrationMaxReferenceBytes;
    std::size_t max_references = kDefaultMigrationMaxReferences;
};

struct MigrationItemMapping {
    std::string source_id;
    std::string target_id;
    std::string uid;
    kano::backlog_core::ItemType type;
    std::string source_path;
    std::string target_path;
};

struct MigrationArtifactMapping {
    std::string owner_source_id;
    std::string owner_target_id;
    std::string source_path;
    std::string target_path;
    std::string sha256;
    std::uintmax_t size_bytes = 0;
};

struct MigrationReferenceRewrite {
    std::string path;
    std::string reference_class;
    std::string source_id;
    std::string target_id;
    std::size_t occurrence_count = 0;
    bool external_to_subtree = false;
};

struct MigrationPlan {
    std::string schema = kMigrationPlanSchema;
    std::string status = "blocked";
    MigrationRequest request;
    std::string source_revision;
    std::string target_revision;
    std::string source_root_id;
    std::string source_root_uid;
    std::string target_prefix;
    std::vector<MigrationItemMapping> items;
    std::vector<MigrationArtifactMapping> artifacts;
    std::vector<MigrationReferenceRewrite> references;
    std::vector<std::string> affected_paths;
    std::vector<std::string> blockers;
    std::vector<std::string> warnings;
    std::string plan_hash;
    bool dry_run = true;
    bool mutates_backlog = false;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct MigrationResult {
    std::string schema = kMigrationResultSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> changed_paths;
    std::vector<std::string> operation_receipts;
    std::string recovery_status;
    bool idempotent_replay = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct MigrationVerification {
    std::string schema = kMigrationVerificationSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> postconditions;
    std::vector<std::string> failures;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct MigrationStatus {
    std::string schema = kMigrationStatusSchema;
    std::string status;
    std::string plan_hash;
    std::string recovery_status;
    bool resume_supported = false;
    bool rollback_supported = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct MigrationRollback {
    std::string schema = kMigrationRollbackSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> restored_paths;
    std::vector<std::string> failures;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

class MigrationOps {
public:
    struct PlanOptions {
        std::filesystem::path start_path = ".";
        std::optional<std::filesystem::path> backlog_root;
        MigrationRequest request;
    };

    struct ApplyOptions {
        PlanOptions plan;
        std::string expected_plan_hash;
        bool confirm = false;

        // Test-only deterministic failure injection. Supported phase names are
        // after_stage, after_target_publish, after_reference_rewrite, and
        // after_source_retire.
        std::optional<std::string> inject_failure_after;
    };

    struct RecoveryOptions {
        std::filesystem::path start_path = ".";
        std::optional<std::filesystem::path> backlog_root;
        std::string plan_hash;
        bool confirm = false;
    };

    /**
     * Build a bounded immutable migration plan without writing backlog files,
     * target sequences, receipts, or caches.
     */
    static MigrationPlan plan(const PlanOptions& options);

    /**
     * Apply an exact immutable plan through a rollback-capable filesystem
     * transaction. Mutation requires confirm=true and an exact plan hash.
     */
    static MigrationResult apply(const ApplyOptions& options);

    /** Verify the persisted transaction and canonical migration postconditions. */
    static MigrationVerification verify(const RecoveryOptions& options);

    /** Inspect a persisted migration transaction without mutating it. */
    static MigrationStatus status(const RecoveryOptions& options);

    /** Restore the exact pre-migration file state. Mutation requires confirm=true. */
    static MigrationRollback rollback(const RecoveryOptions& options);

    /** Return validation diagnostics. An empty vector means the request is valid. */
    static std::vector<std::string> validate_request(const MigrationRequest& request);
};

} // namespace kano::backlog_ops
