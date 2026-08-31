#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace kano::backlog_ops::change_probe {

inline constexpr std::string_view kNtfsUsnProofKind = "windows-ntfs-usn-v1";

enum class Status {
    Clean,
    RelevantChange,
    Unsupported,
    Unavailable,
    RootIdentityChanged,
    JournalReset,
    JournalWrapped,
    Malformed,
    BudgetExhausted,
};

struct RootIdentity {
    std::uint64_t volume_serial = 0;
    std::uint64_t file_id = 0;

    bool operator==(const RootIdentity&) const = default;
};

struct Checkpoint {
    std::string proof_kind = std::string(kNtfsUsnProofKind);
    RootIdentity root;
    std::uint64_t journal_id = 0;
    std::int64_t usn = 0;
};

struct WatchPath {
    std::string source_ref;
    std::filesystem::path path;
    bool directory = false;
};

struct WatchEntry {
    std::string source_ref;
    std::uint64_t file_id = 0;
    bool directory = false;
};

struct CaptureResult {
    Status status = Status::Unavailable;
    std::optional<Checkpoint> checkpoint;
};

struct WatchResult {
    Status status = Status::Unavailable;
    std::vector<WatchEntry> entries;
};

struct VerifyBudget {
    std::size_t max_records = 32768;
    std::size_t max_bytes = 4U * 1024U * 1024U;
    std::chrono::milliseconds max_elapsed{80};
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max();
};

struct VerifyResult {
    Status status = Status::Unavailable;
    std::optional<Checkpoint> endpoint;
    std::size_t records_read = 0;
    std::size_t bytes_read = 0;
    std::uint64_t usn_span = 0;
};

std::string_view reason(Status status);

CaptureResult capture_checkpoint(const std::filesystem::path& product_root);

WatchResult capture_watch_set(
    const std::filesystem::path& product_root,
    const std::vector<WatchPath>& paths
);

VerifyResult verify_window(
    const std::filesystem::path& product_root,
    const Checkpoint& start,
    const std::vector<WatchEntry>& watch_set,
    const std::unordered_set<std::uint64_t>& allowed_file_ids = {},
    const VerifyBudget& budget = {}
);

}
