#include "change_probe.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <utility>

#ifdef _WIN32

constexpr std::size_t kMaximumWatchEntries = 131072;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>

#ifndef FSCTL_READ_UNPRIVILEGED_USN_JOURNAL
#define FSCTL_READ_UNPRIVILEGED_USN_JOURNAL \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 234, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#endif

namespace kano::backlog_ops::change_probe {
namespace {

#ifdef _WIN32

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() {
        if (value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value_ != INVALID_HANDLE_VALUE) {
                CloseHandle(value_);
            }
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const { return value_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const { return value_; }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

struct VolumeContext {
    Handle volume;
    std::filesystem::path root_path;
    RootIdentity root_identity;
};

struct ReadUsnJournalDataV1 {
    USN start_usn;
    DWORD reason_mask;
    DWORD return_only_on_close;
    DWORDLONG timeout;
    DWORDLONG bytes_to_wait_for;
    DWORDLONG journal_id;
    WORD min_major_version;
    WORD max_major_version;
};

std::uint64_t file_id(const BY_HANDLE_FILE_INFORMATION& info) {
    return (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U)
        | static_cast<std::uint64_t>(info.nFileIndexLow);
}

std::optional<BY_HANDLE_FILE_INFORMATION> handle_information(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) {
        return std::nullopt;
    }
    return info;
}

std::optional<VolumeContext> open_volume(const std::filesystem::path& product_root) {
    std::array<wchar_t, MAX_PATH + 1> mount_path{};
    if (!GetVolumePathNameW(product_root.c_str(), mount_path.data(), static_cast<DWORD>(mount_path.size()))) {
        return std::nullopt;
    }

    std::array<wchar_t, MAX_PATH + 1> file_system{};
    if (!GetVolumeInformationW(
            mount_path.data(),
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            file_system.data(),
            static_cast<DWORD>(file_system.size()))) {
        return std::nullopt;
    }
    if (_wcsicmp(file_system.data(), L"NTFS") != 0) {
        return std::nullopt;
    }

    std::array<wchar_t, 64> volume_name{};
    if (!GetVolumeNameForVolumeMountPointW(
            mount_path.data(), volume_name.data(), static_cast<DWORD>(volume_name.size()))) {
        return std::nullopt;
    }
    const std::size_t length = std::wcslen(volume_name.data());
    if (length == 0) {
        return std::nullopt;
    }
    if (volume_name[length - 1] == L'\\') {
        volume_name[length - 1] = L'\0';
    }

    Handle volume(CreateFileW(
        volume_name.data(),
        FILE_TRAVERSE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!volume.valid()) {
        return std::nullopt;
    }

    Handle root(CreateFileW(
        product_root.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr));
    if (!root.valid()) {
        return std::nullopt;
    }
    const auto info = handle_information(root.get());
    if (!info.has_value()) {
        return std::nullopt;
    }

    VolumeContext context;
    context.volume = std::move(volume);
    context.root_path = std::filesystem::absolute(product_root).lexically_normal();
    context.root_identity = {
        static_cast<std::uint64_t>(info->dwVolumeSerialNumber),
        file_id(*info),
    };
    return context;
}

std::optional<USN_JOURNAL_DATA_V0> query_journal(HANDLE volume) {
    USN_JOURNAL_DATA_V0 journal{};
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(
            volume,
            FSCTL_QUERY_USN_JOURNAL,
            nullptr,
            0,
            &journal,
            sizeof(journal),
            &bytes_returned,
            nullptr)
        || bytes_returned < sizeof(journal)) {
        return std::nullopt;
    }
    return journal;
}

Checkpoint make_checkpoint(
    const RootIdentity& root,
    const USN_JOURNAL_DATA_V0& journal,
    const USN usn
) {
    return {
        std::string(kNtfsUsnProofKind),
        root,
        static_cast<std::uint64_t>(journal.UsnJournalID),
        static_cast<std::int64_t>(usn),
    };
}

std::optional<WatchEntry> capture_path(
    const WatchPath& watch_path,
    const std::uint64_t expected_volume,
    const bool directory
) {
    Handle handle(CreateFileW(
        watch_path.path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!handle.valid()) {
        return std::nullopt;
    }
    const auto info = handle_information(handle.get());
    if (!info.has_value() || static_cast<std::uint64_t>(info->dwVolumeSerialNumber) != expected_volume) {
        return std::nullopt;
    }
    return WatchEntry{watch_path.source_ref, file_id(*info), directory};
}

bool is_within_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const auto normalized_root = std::filesystem::absolute(root).lexically_normal();
    const auto normalized_candidate = std::filesystem::absolute(candidate).lexically_normal();
    const auto relative = normalized_candidate.lexically_relative(normalized_root);
    if (relative.empty()) {
        return normalized_candidate == normalized_root;
    }
    return *relative.begin() != "..";
}

std::chrono::steady_clock::time_point effective_deadline(
    const VerifyBudget& budget,
    const std::chrono::steady_clock::time_point started_at
) {
    const auto elapsed_deadline = budget.max_elapsed <= std::chrono::milliseconds::zero()
        ? started_at
        : started_at + budget.max_elapsed;
    return std::min(budget.deadline, elapsed_deadline);
}

bool deadline_expired(
    const std::chrono::steady_clock::time_point deadline
) {
    return std::chrono::steady_clock::now() >= deadline;
}

std::uint64_t verified_usn_span(
    const std::int64_t start,
    const std::int64_t endpoint
) {
    return endpoint <= start
        ? 0
        : static_cast<std::uint64_t>(endpoint - start);
}

#endif

}

std::string_view reason(const Status status) {
    switch (status) {
    case Status::Clean:
        return "change_proof_clean";
    case Status::RelevantChange:
        return "change_proof_relevant_change";
    case Status::Unsupported:
        return "change_proof_unsupported";
    case Status::Unavailable:
        return "change_proof_unavailable";
    case Status::RootIdentityChanged:
        return "change_proof_root_identity_changed";
    case Status::JournalReset:
        return "change_proof_journal_reset";
    case Status::JournalWrapped:
        return "change_proof_journal_wrapped";
    case Status::Malformed:
        return "change_proof_malformed";
    case Status::BudgetExhausted:
        return "change_proof_budget_exhausted";
    }
    return "change_proof_unavailable";
}

CaptureResult capture_checkpoint(const std::filesystem::path& product_root) {
#ifdef _WIN32
    auto context = open_volume(product_root);
    if (!context.has_value()) {
        return {Status::Unavailable, std::nullopt};
    }
    const auto journal = query_journal(context->volume.get());
    if (!journal.has_value()) {
        return {Status::Unavailable, std::nullopt};
    }
    return {
        Status::Clean,
        make_checkpoint(context->root_identity, *journal, journal->NextUsn),
    };
#else
    static_cast<void>(product_root);
    return {Status::Unsupported, std::nullopt};
#endif
}

WatchResult capture_watch_set(
    const std::filesystem::path& product_root,
    const std::vector<WatchPath>& paths
) {
#ifdef _WIN32
    const auto context = open_volume(product_root);
    if (!context.has_value()) {
        return {Status::Unavailable, {}};
    }

    std::vector<WatchEntry> entries;
    std::unordered_map<std::uint64_t, std::size_t> directories;
    const auto add_directory = [&](const std::filesystem::path& directory_path) {
        const auto directory = capture_path(
            WatchPath{"", directory_path, true}, context->root_identity.volume_serial, true);
        if (!directory.has_value()) {
            return Status::Unavailable;
        }
        if (!directories.contains(directory->file_id)) {
            if (entries.size() >= kMaximumWatchEntries) {
                return Status::BudgetExhausted;
            }
            directories.emplace(directory->file_id, entries.size());
            entries.push_back(*directory);
        }
        return Status::Clean;
    };
    for (const auto& path : paths) {
        if (!is_within_root(context->root_path, path.path)) {
            return {Status::Malformed, {}};
        }
        if (path.directory) {
            const auto status = add_directory(path.path);
            if (status != Status::Clean) {
                return {status, {}};
            }
        } else {
            const auto item = capture_path(path, context->root_identity.volume_serial, false);
            if (!item.has_value()) {
                return {Status::Unavailable, {}};
            }
            if (entries.size() >= kMaximumWatchEntries) {
                return {Status::BudgetExhausted, {}};
            }
            entries.push_back(*item);
        }

        auto parent = std::filesystem::absolute(path.path).lexically_normal().parent_path();
        while (parent != context->root_path
            && is_within_root(context->root_path, parent)) {
            const auto status = add_directory(parent);
            if (status != Status::Clean) {
                return {status, {}};
            }
            const auto next = parent.parent_path();
            if (next == parent) {
                return {Status::Malformed, {}};
            }
            parent = next;
        }
    }
    return {Status::Clean, std::move(entries)};
#else
    static_cast<void>(product_root);
    static_cast<void>(paths);
    return {Status::Unsupported, {}};
#endif
}

VerifyResult verify_window(
    const std::filesystem::path& product_root,
    const Checkpoint& start,
    const std::vector<WatchEntry>& watch_set,
    const std::unordered_set<std::uint64_t>& allowed_file_ids,
    const VerifyBudget& budget
) {
#ifdef _WIN32
    const auto started_at = std::chrono::steady_clock::now();
    const auto deadline = effective_deadline(budget, started_at);
    const auto exhausted = [&](const std::size_t records,
                               const std::size_t bytes,
                               const std::int64_t verified_usn) {
        return VerifyResult{
            Status::BudgetExhausted,
            std::nullopt,
            records,
            bytes,
            verified_usn_span(start.usn, verified_usn),
        };
    };
    if (deadline_expired(deadline)) {
        return exhausted(0, 0, start.usn);
    }
    if (start.proof_kind != kNtfsUsnProofKind || start.usn < 0) {
        return {Status::Malformed};
    }
    if (deadline_expired(deadline)) {
        return exhausted(0, 0, start.usn);
    }
    auto context = open_volume(product_root);
    if (deadline_expired(deadline)) {
        return exhausted(0, 0, start.usn);
    }
    if (!context.has_value()) {
        return {Status::Unavailable};
    }
    if (context->root_identity != start.root) {
        return {Status::RootIdentityChanged};
    }
    if (deadline_expired(deadline)) {
        return exhausted(0, 0, start.usn);
    }
    const auto journal = query_journal(context->volume.get());
    if (deadline_expired(deadline)) {
        return exhausted(0, 0, start.usn);
    }
    if (!journal.has_value()) {
        return {Status::Unavailable};
    }
    if (start.journal_id != static_cast<std::uint64_t>(journal->UsnJournalID)) {
        return {Status::JournalReset};
    }
    if (start.usn < journal->FirstUsn || start.usn < journal->LowestValidUsn) {
        return {Status::JournalWrapped};
    }
    if (start.usn > journal->NextUsn) {
        return {Status::JournalReset};
    }

    const auto endpoint = make_checkpoint(context->root_identity, *journal, journal->NextUsn);
    std::unordered_set<std::uint64_t> watched_files;
    std::unordered_set<std::uint64_t> watched_directories;
    watched_files.reserve(watch_set.size());
    watched_directories.reserve(watch_set.size());
    for (const auto& entry : watch_set) {
        if (deadline_expired(deadline)) {
            return exhausted(0, 0, start.usn);
        }
        if (entry.file_id == 0) {
            return {Status::Malformed};
        }
        (entry.directory ? watched_directories : watched_files).insert(entry.file_id);
    }

    constexpr std::size_t kReadBufferSize = 64U * 1024U;
    std::array<std::byte, kReadBufferSize> buffer{};
    std::int64_t cursor = start.usn;
    std::size_t total_records = 0;
    std::size_t total_bytes = 0;

    while (cursor < endpoint.usn) {
        if (total_records >= budget.max_records || total_bytes >= budget.max_bytes
            || deadline_expired(deadline)) {
            return exhausted(total_records, total_bytes, cursor);
        }

        ReadUsnJournalDataV1 request{
            static_cast<USN>(cursor),
            std::numeric_limits<DWORD>::max(),
            FALSE,
            0,
            0,
            static_cast<DWORDLONG>(start.journal_id),
            2,
            2,
        };
        DWORD bytes_returned = 0;
        if (deadline_expired(deadline)) {
            return exhausted(total_records, total_bytes, cursor);
        }
        if (!DeviceIoControl(
                context->volume.get(),
                FSCTL_READ_UNPRIVILEGED_USN_JOURNAL,
                &request,
                sizeof(request),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_returned,
                nullptr)) {
            return {Status::Unavailable, endpoint, total_records, total_bytes};
        }
        if (deadline_expired(deadline)) {
            return exhausted(total_records, total_bytes, cursor);
        }
        if (bytes_returned < sizeof(USN)) {
            return {Status::Malformed, endpoint, total_records, total_bytes};
        }
        total_bytes += bytes_returned;
        if (total_bytes > budget.max_bytes) {
            return exhausted(total_records, total_bytes, cursor);
        }

        USN next_usn = 0;
        std::memcpy(&next_usn, buffer.data(), sizeof(next_usn));
        if (next_usn <= cursor) {
            return {Status::Malformed, endpoint, total_records, total_bytes};
        }

        std::size_t offset = sizeof(USN);
        while (offset < bytes_returned) {
            if (deadline_expired(deadline)) {
                return exhausted(total_records, total_bytes, cursor);
            }
            if (bytes_returned - offset < sizeof(USN_RECORD_V2)) {
                return {Status::Malformed, endpoint, total_records, total_bytes};
            }
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(buffer.data() + offset);
            if (record->RecordLength < sizeof(USN_RECORD_V2)
                || record->RecordLength > bytes_returned - offset
                || record->MajorVersion != 2) {
                return {Status::Malformed, endpoint, total_records, total_bytes};
            }
            ++total_records;
            if (total_records > budget.max_records) {
                return exhausted(total_records, total_bytes, cursor);
            }
            if (record->Usn >= start.usn && record->Usn < endpoint.usn) {
                const auto changed_id = static_cast<std::uint64_t>(record->FileReferenceNumber);
                const auto parent_id = static_cast<std::uint64_t>(record->ParentFileReferenceNumber);
                const auto relevant_match =
                    (watched_files.contains(changed_id) ? 1U : 0U)
                    | (watched_directories.contains(changed_id) ? 2U : 0U)
                    | (watched_directories.contains(parent_id) ? 4U : 0U);
                const bool relevant = relevant_match != 0;
                if (relevant && !allowed_file_ids.contains(changed_id)) {
                    return {
                        Status::RelevantChange,
                        endpoint,
                        total_records,
                        total_bytes,
                        verified_usn_span(start.usn, record->Usn),
                    };
                }
            }
            offset += record->RecordLength;
        }
        cursor = std::min<std::int64_t>(static_cast<std::int64_t>(next_usn), endpoint.usn);
    }
    if (deadline_expired(deadline)) {
        return exhausted(total_records, total_bytes, cursor);
    }
    return {
        Status::Clean,
        endpoint,
        total_records,
        total_bytes,
        verified_usn_span(start.usn, endpoint.usn),
    };
#else
    static_cast<void>(product_root);
    static_cast<void>(start);
    static_cast<void>(watch_set);
    static_cast<void>(allowed_file_ids);
    static_cast<void>(budget);
    return {Status::Unsupported};
#endif
}

}
