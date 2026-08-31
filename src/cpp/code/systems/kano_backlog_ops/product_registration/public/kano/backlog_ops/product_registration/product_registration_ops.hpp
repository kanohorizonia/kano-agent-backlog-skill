#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kano::backlog_ops {

inline constexpr const char* kProductRegistrationPlanSchema =
    "kob.product_registration.plan.v1";
inline constexpr const char* kProductRegistrationResultSchema =
    "kob.product_registration.result.v1";
inline constexpr const char* kProductRegistrationVerificationSchema =
    "kob.product_registration.verification.v1";
inline constexpr const char* kProductRegistrationStatusSchema =
    "kob.product_registration.status.v1";
inline constexpr const char* kProductRegistrationReceiptSchema =
    "kob.product_registration.receipt.v1";
inline constexpr const char* kProductRegistrationJournalSchema =
    "kob.product_registration.journal.v1";

inline constexpr std::size_t kDefaultProductRegistrationMaxFiles = 500000;
inline constexpr std::uintmax_t kDefaultProductRegistrationMaxBytes =
    16ull * 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kDefaultProductRegistrationMaxItems = 50000;

struct ProductRegistrationRequest {
    std::string product;
    std::string product_name;
    std::string prefix;
    std::vector<std::string> aliases;
    std::vector<std::string> repo_bindings;
    std::filesystem::path external_root;
};

struct ProductRegistrationLimits {
    std::size_t max_files = kDefaultProductRegistrationMaxFiles;
    std::uintmax_t max_bytes = kDefaultProductRegistrationMaxBytes;
    std::size_t max_items = kDefaultProductRegistrationMaxItems;
};

struct ProductRegistrationFile {
    std::string ref;
    std::string kind;
    std::uintmax_t size = 0;
    std::string sha256;
};

struct ProductRegistrationIdentity {
    std::string id;
    std::string uid;
    std::string source_ref;
    std::string source_sha256;
};

struct ProductRegistrationPlan {
    std::string schema = kProductRegistrationPlanSchema;
    std::string status = "blocked";
    ProductRegistrationRequest request;
    ProductRegistrationLimits limits;
    std::string product;
    std::string product_name;
    std::string prefix;
    std::vector<std::string> aliases;
    std::vector<std::string> repo_bindings;
    std::string config_ref;
    std::string source_root_ref;
    std::string canonical_destination_ref;
    std::string config_path_digest;
    std::string external_root_path_digest;
    std::string canonical_destination_path_digest;
    std::string source_revision;
    std::string config_revision;
    std::string proposed_config_revision;
    std::string registry_revision;
    std::vector<ProductRegistrationFile> files;
    std::vector<ProductRegistrationIdentity> identities;
    std::vector<std::string> safety_checks;
    std::vector<std::string> blockers;
    std::vector<std::string> warnings;
    std::string plan_hash;
    bool canonical_destination_absent = false;
    bool dry_run = true;
    bool mutates_backlog = false;
    bool external_root_read_only = true;
    bool creates_canonical_destination = false;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRegistrationResult {
    std::string schema = kProductRegistrationResultSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> changed_refs;
    std::vector<std::string> operation_receipts;
    std::string receipt_ref;
    std::string recovery_status;
    std::optional<std::string> apply_agent;
    std::optional<std::string> recovery_agent;
    bool idempotent_replay = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRegistrationVerification {
    std::string schema = kProductRegistrationVerificationSchema;
    std::string status;
    std::string plan_hash;
    std::vector<std::string> postconditions;
    std::vector<std::string> failures;
    std::optional<std::string> apply_agent;
    std::optional<std::string> recovery_agent;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

struct ProductRegistrationStatus {
    std::string schema = kProductRegistrationStatusSchema;
    std::string status;
    std::string plan_hash;
    std::string stage;
    std::string recovery_status;
    std::optional<std::string> apply_agent;
    std::optional<std::string> recovery_agent;
    bool rollback_supported = false;

    [[nodiscard]] std::string to_json(bool pretty = false) const;
};

class ProductRegistrationOps {
public:
    struct PlanOptions {
        std::filesystem::path backlog_root;
        ProductRegistrationRequest request;
        ProductRegistrationLimits limits;
    };

    struct ApplyOptions {
        PlanOptions plan;
        std::string expected_plan_hash;
        std::optional<std::string> agent;
        bool confirm = false;

        // Test-only deterministic hooks; never exposed through CLI or env.
        std::optional<std::string> inject_failure_after;
        std::optional<std::string> inject_interruption_after;
        std::optional<std::string> inject_process_exit_after;
        int injected_process_exit_code = 86;
        std::function<void(std::string_view)> lock_test_checkpoint;
    };

    struct RecoveryOptions {
        std::filesystem::path backlog_root;
        std::string plan_hash;
    };

    static ProductRegistrationPlan plan(const PlanOptions& options);
    static ProductRegistrationResult apply(const ApplyOptions& options);
    static ProductRegistrationVerification verify(
        const RecoveryOptions& options);
    static ProductRegistrationStatus status(const RecoveryOptions& options);
};

} // namespace kano::backlog_ops
