#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kano::backlog_ops {

inline constexpr const char* kProductRelocationPlanSchema =
    "kob.product_root_relocation.plan.v1";
inline constexpr const char* kProductRelocationResultSchema =
    "kob.product_root_relocation.result.v1";
inline constexpr const char* kProductRelocationVerificationSchema =
    "kob.product_root_relocation.verification.v1";
inline constexpr const char* kProductRelocationStatusSchema =
    "kob.product_root_relocation.status.v1";
inline constexpr const char* kProductRelocationRollbackSchema =
    "kob.product_root_relocation.rollback.v1";
inline constexpr std::size_t kDefaultProductRelocationMaxFiles = 500000;
inline constexpr std::uintmax_t kDefaultProductRelocationMaxBytes =
    16ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultProductRelocationMaxItems = 50000;

struct ProductRelocationRequest {
    std::string product;
    std::filesystem::path destination_root;
    std::optional<std::string> expected_source_revision;
    std::size_t max_files = kDefaultProductRelocationMaxFiles;
    std::uintmax_t max_bytes = kDefaultProductRelocationMaxBytes;
    std::size_t max_items = kDefaultProductRelocationMaxItems;
};

struct ProductRelocationFile {
    std::string ref;
    std::string kind;
    std::uintmax_t size = 0;
    std::string sha256;
    bool preserves_bytes = true;
};

struct ProductRelocationIdentity {
    std::string id;
    std::string uid;
    std::string source_ref;
    std::string source_sha256;
};

struct ProductRelocationPlan {
    std::string schema = kProductRelocationPlanSchema;
    std::string status = "blocked";
    ProductRelocationRequest request;
    std::string product;
    std::string prefix;
    std::string source_root_ref;
    std::string destination_root_ref;
    std::string config_ref;
    std::string destination_path_digest;
    std::string source_revision;
    std::string config_revision;
    std::string sequence_state_revision;
    std::size_t sequence_count = 0;
    std::size_t reservation_count = 0;
    std::string relocation_strategy = "copy-verify-publish-retire";
    bool cross_volume_safe = true;
    bool destination_preexisted_empty = false;
    std::vector<ProductRelocationFile> files;
    std::vector<ProductRelocationIdentity> identities;
    std::vector<std::string> reference_checks;
    std::vector<std::string> derived_surfaces;
    std::vector<std::string> generated_rebuilders;
    std::vector<std::string> validation_steps;
    std::vector<std::string> blockers;
    std::vector<std::string> warnings;
    std::string plan_hash;
    bool dry_run = true;
    bool mutates_backlog = false;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRelocationResult {
    std::string schema = kProductRelocationResultSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> changed_refs;
    std::vector<std::string> operation_receipts;
    std::string receipt_ref;
    std::string recovery_status;
    bool idempotent_replay = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRelocationVerification {
    std::string schema = kProductRelocationVerificationSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> postconditions;
    std::vector<std::string> failures;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRelocationStatus {
    std::string schema = kProductRelocationStatusSchema;
    std::string status;
    std::string plan_hash;
    std::string stage;
    std::string recovery_status;
    bool rollback_supported = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRelocationRollback {
    std::string schema = kProductRelocationRollbackSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> restored_refs;
    std::vector<std::string> failures;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

class ProductRelocationOps {
public:
    struct PlanOptions {
        std::filesystem::path start_path = ".";
        std::optional<std::filesystem::path> backlog_root;
        ProductRelocationRequest request;
    };

    struct ApplyOptions {
        PlanOptions plan;
        std::string expected_plan_hash;
        bool confirm = false;

        // Test-only deterministic failure and recoverable interruption hooks.
        std::optional<std::string> inject_failure_after;
        std::optional<std::string> inject_interruption_after;
    };

    struct RecoveryOptions {
        std::filesystem::path start_path = ".";
        std::optional<std::filesystem::path> backlog_root;
        std::string plan_hash;
        bool confirm = false;
    };

    static ProductRelocationPlan plan(const PlanOptions& options);
    static ProductRelocationResult apply(const ApplyOptions& options);
    static ProductRelocationVerification verify(const RecoveryOptions& options);
    static ProductRelocationStatus status(const RecoveryOptions& options);
    static ProductRelocationRollback rollback(const RecoveryOptions& options);
};

} // namespace kano::backlog_ops
