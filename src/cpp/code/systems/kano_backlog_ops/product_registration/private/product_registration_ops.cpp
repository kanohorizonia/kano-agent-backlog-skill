#include "kano/backlog_ops/product_registration/product_registration_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/errors.hpp"

#include <json/json.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ItemType;
using kano::backlog_core::ParseError;
using kano::backlog_core::ProductDefinition;
using kano::backlog_core::ProjectConfig;
using kano::backlog_ops::ProductRegistrationFile;
using kano::backlog_ops::ProductRegistrationIdentity;
using kano::backlog_ops::ProductRegistrationLimits;
using kano::backlog_ops::ProductRegistrationPlan;
using kano::backlog_ops::ProductRegistrationRequest;

constexpr std::uintmax_t kMaximumSingleFileBytes =
    256ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumFrontmatterBytes = 1u * 1024u * 1024u;
constexpr std::size_t kMaximumPublicEntryLimit = 1000000u;
constexpr std::uintmax_t kMaximumAggregateBytes =
    256ull * 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumPlanCacheEntries = 128;

constexpr std::size_t bounded_entry_limit(std::size_t limit) noexcept {
    constexpr std::size_t multiplier = 4u;
    constexpr std::size_t allowance = 64u;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    return limit > (maximum - allowance) / multiplier
        ? maximum
        : limit * multiplier + allowance;
}

class PathEntryBudget {
public:
    explicit PathEntryBudget(std::size_t public_limit)
        : remaining_(bounded_entry_limit(public_limit)) {}

    bool charge() noexcept {
        if (remaining_ == 0u) {
            return false;
        }
        --remaining_;
        return true;
    }

private:
    std::size_t remaining_;
};

struct RegistryScanBudget {
    RegistryScanBudget(std::size_t maximum_items, std::size_t source_items)
        : paths(maximum_items),
          remaining_items(
              source_items >= maximum_items
                  ? 0u
                  : maximum_items - source_items) {}

    PathEntryBudget paths;
    std::size_t remaining_items;
    bool path_limit_exceeded = false;
    bool item_limit_exceeded = false;

    [[nodiscard]] bool exhausted() const noexcept {
        return path_limit_exceeded || item_limit_exceeded;
    }
};

std::string lowercase_ascii(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool is_valid_product_slug(const std::string& value) {
    static const std::regex grammar(
        R"(^[A-Za-z0-9](?:[A-Za-z0-9_-]{0,62}[A-Za-z0-9])?$)");
    return std::regex_match(value, grammar);
}

bool is_valid_prefix(const std::string& value) {
    static const std::regex grammar(R"(^[A-Z][A-Z0-9]{1,15}$)");
    return std::regex_match(value, grammar);
}

bool is_valid_product_name(const std::string& value) {
    if (value.empty() || trim_copy(value).empty() || value.size() > 256) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 0x20u || ch == 0x7fu;
    });
}

bool is_valid_agent(const std::string& agent) {
    static const std::regex grammar(R"(^[A-Za-z][A-Za-z0-9._-]{0,63}$)");
    static const std::set<std::string> placeholders = {
        "agent", "anonymous", "assistant", "auto", "na", "none",
        "null", "placeholder", "todo", "unknown", "unset", "user",
    };
    return std::regex_match(agent, grammar) &&
           !placeholders.contains(lowercase_ascii(agent));
}

std::filesystem::path normalized_absolute(
    const std::filesystem::path& path
) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }
    if (error) {
        throw std::runtime_error("path_normalization_failed");
    }
    return normalized.lexically_normal();
}

bool path_component_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) {
#ifdef _WIN32
    return CompareStringOrdinal(
               left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

bool path_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) {
    const auto normalized_left = left.lexically_normal();
    const auto normalized_right = right.lexically_normal();
    auto left_iterator = normalized_left.begin();
    auto right_iterator = normalized_right.begin();
    for (; left_iterator != normalized_left.end() &&
           right_iterator != normalized_right.end();
         ++left_iterator, ++right_iterator) {
        if (!path_component_equal(*left_iterator, *right_iterator)) {
            return false;
        }
    }
    return left_iterator == normalized_left.end() &&
           right_iterator == normalized_right.end();
}

bool is_within_or_equal(
    const std::filesystem::path& child,
    const std::filesystem::path& parent
) {
    const auto normalized_child = child.lexically_normal();
    const auto normalized_parent = parent.lexically_normal();
    auto child_iterator = normalized_child.begin();
    for (auto parent_iterator = normalized_parent.begin();
         parent_iterator != normalized_parent.end();
         ++parent_iterator, ++child_iterator) {
        if (child_iterator == normalized_child.end() ||
            !path_component_equal(*child_iterator, *parent_iterator)) {
            return false;
        }
    }
    return true;
}

bool path_entry_is_reparse(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(
        std::filesystem::symlink_status(path, error));
#endif
}

bool existing_path_has_reparse_component(
    const std::filesystem::path& absolute_path
) {
    if (!absolute_path.is_absolute()) {
        return true;
    }
    std::filesystem::path current = absolute_path.root_path();
    for (const auto& component : absolute_path.relative_path()) {
        if (component == ".") {
            continue;
        }
        if (component == "..") {
            current = current.parent_path();
            continue;
        }
        current /= component;
        std::error_code error;
        if (!std::filesystem::exists(current, error)) {
            if (error) {
                return true;
            }
            break;
        }
        if (path_entry_is_reparse(current)) {
            return true;
        }
    }
    return false;
}

struct ExistingPathIdentity {
    std::filesystem::path final_path;
    std::uint64_t volume = 0;
    std::uint64_t file = 0;
    bool is_directory = false;
};

#ifdef _WIN32
std::filesystem::path path_from_final_handle(HANDLE handle) {
    const auto flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const auto required = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
    if (required == 0) {
        throw std::runtime_error("final_path_identity_failed");
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1u);
    const auto written = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= buffer.size()) {
        throw std::runtime_error("final_path_identity_failed");
    }
    std::wstring value(buffer.data(), written);
    constexpr std::wstring_view unc_prefix = L"\\\\?\\UNC\\";
    constexpr std::wstring_view path_prefix = L"\\\\?\\";
    if (value.starts_with(unc_prefix)) {
        value = L"\\\\" + value.substr(unc_prefix.size());
    } else if (value.starts_with(path_prefix)) {
        value = value.substr(path_prefix.size());
    }
    return std::filesystem::path(value).lexically_normal();
}
#endif

ExistingPathIdentity existing_path_identity(
    const std::filesystem::path& raw_path
) {
    if (!raw_path.is_absolute() ||
        existing_path_has_reparse_component(raw_path)) {
        throw std::runtime_error("path_reparse_or_alias_not_supported");
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(raw_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error("path_identity_not_found");
    }
    const HANDLE handle = CreateFileW(
        raw_path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("path_identity_open_failed");
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        CloseHandle(handle);
        throw std::runtime_error("path_identity_query_failed");
    }
    ExistingPathIdentity identity;
    try {
        identity.final_path = path_from_final_handle(handle);
    } catch (...) {
        CloseHandle(handle);
        throw;
    }
    CloseHandle(handle);
    identity.volume = information.dwVolumeSerialNumber;
    identity.file =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) |
        information.nFileIndexLow;
    identity.is_directory =
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return identity;
#else
    std::error_code error;
    ExistingPathIdentity identity;
    identity.final_path = std::filesystem::canonical(raw_path, error);
    if (error) {
        throw std::runtime_error("path_identity_query_failed");
    }
    identity.is_directory = std::filesystem::is_directory(
        identity.final_path, error);
    if (error) {
        throw std::runtime_error("path_identity_query_failed");
    }
    return identity;
#endif
}

bool same_existing_identity(
    const ExistingPathIdentity& left,
    const ExistingPathIdentity& right
) {
#ifdef _WIN32
    return left.volume == right.volume && left.file == right.file;
#else
    return path_equal(left.final_path, right.final_path);
#endif
}

bool is_direct_child(
    const std::filesystem::path& child,
    const std::filesystem::path& parent
) {
    return !child.filename().empty() &&
           path_equal(child.parent_path(), parent);
}

std::string current_utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::uint64_t current_process_id_value() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::string unique_operation_token() {
    static std::atomic<std::uint64_t> counter{0};
    std::ostringstream token;
    token << std::hex << current_process_id_value() << '-'
          << std::chrono::steady_clock::now().time_since_epoch().count()
          << '-' << counter.fetch_add(1, std::memory_order_relaxed);
    return token.str();
}

bool is_valid_operation_token(std::string_view token) {
    if (token.empty() || token.size() > 128u) {
        return false;
    }
    std::size_t separators = 0;
    bool segment_has_digit = false;
    for (const auto ch : token) {
        if (ch == '-') {
            if (!segment_has_digit || ++separators > 2u) {
                return false;
            }
            segment_has_digit = false;
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        segment_has_digit = true;
    }
    return separators == 2u && segment_has_digit;
}

std::string bounded_error(const std::string& value) {
    static const std::regex windows_path(R"([A-Za-z]:[\\/])");
    if (std::regex_search(value, windows_path) ||
        value.find('\\') != std::string::npos || value.starts_with("/") ||
        value.find(":/") != std::string::npos) {
        return "product_registration_operation_failed:details_redacted";
    }
    return value;
}

Json::Value parse_json(const std::string& content);
bool is_lowercase_sha256(const std::string& value);

template <typename T>
void sort_unique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::vector<std::string> normalize_selector_list(
    const std::vector<std::string>& selectors,
    bool& has_empty_selector
) {
    std::vector<std::string> normalized;
    normalized.reserve(selectors.size());
    for (const auto& selector : selectors) {
        auto value = ProjectConfig::normalize_product_selector(selector);
        if (value.empty()) {
            has_empty_selector = true;
            continue;
        }
        normalized.push_back(std::move(value));
    }
    sort_unique(normalized);
    return normalized;
}

std::string read_file(
    const std::filesystem::path& path,
    std::uintmax_t maximum_bytes = kMaximumSingleFileBytes
) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_bytes) {
        throw std::runtime_error(
            error ? "file_stat_failed" : "single_file_limit_exceeded");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("file_read_failed");
    }
    std::string content(static_cast<std::size_t>(size), '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("file_read_failed");
    }
    return content;
}

bool write_file_exclusive(
    const std::filesystem::path& path,
    const std::string& content
) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            return false;
        }
        throw std::runtime_error(
            "exclusive_file_create_failed:win32_" +
            std::to_string(error));
    }
    bool success = true;
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(
                handle, content.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            success = false;
            break;
        }
        offset += written;
    }
    if (success && !FlushFileBuffers(handle)) {
        success = false;
    }
    CloseHandle(handle);
    if (!success) {
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        throw std::runtime_error("exclusive_file_write_failed");
    }
#else
    const int descriptor = ::open(
        path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            return false;
        }
        throw std::runtime_error("exclusive_file_create_failed");
    }
    bool success = true;
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto written = ::write(
            descriptor, content.data() + offset, content.size() - offset);
        if (written <= 0) {
            success = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (success && ::fsync(descriptor) != 0) {
        success = false;
    }
    ::close(descriptor);
    if (!success) {
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        throw std::runtime_error("exclusive_file_write_failed");
    }
#endif
    return true;
}

std::string compact_plan_token(const std::string& plan_hash) {
    if (!is_lowercase_sha256(plan_hash)) {
        throw std::runtime_error("invalid_product_registration_plan_hash");
    }
    return plan_hash.substr(0, 16u);
}

std::string atomic_stage_prefix(
    const std::string& plan_hash,
    std::string_view role
) {
    return ".kob-pr." + compact_plan_token(plan_hash) + "." +
           std::string(role) + ".";
}

void reclaim_atomic_stages(
    const std::filesystem::path& target,
    const std::filesystem::path& staging_parent,
    const std::string& content,
    const std::string& plan_hash,
    std::string_view role
) {
    const auto target_parent = existing_path_identity(target.parent_path());
    const auto stage_parent = existing_path_identity(staging_parent);
    if (!target_parent.is_directory || !stage_parent.is_directory ||
        !is_direct_child(target, target_parent.final_path)) {
        throw std::runtime_error("atomic_publish_parent_unsafe");
    }
    const auto prefix = atomic_stage_prefix(plan_hash, role);
    std::size_t matches = 0;
    std::error_code iterator_error;
    for (std::filesystem::directory_iterator iterator(
             stage_parent.final_path,
             std::filesystem::directory_options::none,
             iterator_error), end;
         iterator != end && !iterator_error;
         iterator.increment(iterator_error)) {
        const auto name = iterator->path().filename().generic_string();
        if (!name.starts_with(prefix)) {
            continue;
        }
        if (matches++ >= 128u) {
            break;
        }
        const auto token = name.substr(prefix.size());
        if (!is_valid_operation_token(token) ||
            !is_direct_child(iterator->path(), stage_parent.final_path) ||
            path_entry_is_reparse(iterator->path()) ||
            !iterator->is_regular_file()) {
            continue;
        }
        bool valid = false;
        try {
            const auto staged_content = read_file(
                iterator->path(), 64u * 1024u * 1024u);
            valid = staged_content == content;
            if (!valid && role == "journal") {
                const auto journal = parse_json(staged_content);
                valid = journal["schema"].asString() ==
                            kano::backlog_ops::
                                kProductRegistrationJournalSchema &&
                        journal["plan_hash"].asString() == plan_hash;
            }
        } catch (const std::exception&) {
            valid = false;
        }
        if (!valid) {
            continue;
        }
        std::error_code remove_error;
        std::filesystem::remove(iterator->path(), remove_error);
        if (remove_error) {
            break;
        }
    }
}

void replace_file_atomic(
    const std::filesystem::path& path,
    const std::string& content,
    const std::string& plan_hash,
    std::string_view role,
    const std::filesystem::path& staging_parent = {}
) {
    if (!std::filesystem::is_directory(path.parent_path())) {
        throw std::runtime_error("atomic_publish_parent_missing");
    }
    const auto temporary_parent = staging_parent.empty()
        ? path.parent_path()
        : staging_parent;
    reclaim_atomic_stages(
        path, temporary_parent, content, plan_hash, role);
    std::filesystem::path temporary;
    bool created = false;
    for (std::size_t attempt = 0; attempt < 32u && !created; ++attempt) {
        temporary = temporary_parent /
            (atomic_stage_prefix(plan_hash, role) +
             unique_operation_token());
        created = write_file_exclusive(temporary, content);
    }
    if (!created) {
        throw std::runtime_error("atomic_stage_exclusive_create_failed");
    }
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code remove_error;
        std::filesystem::remove(temporary, remove_error);
        throw std::runtime_error("atomic_publish_failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("atomic_publish_failed");
    }
#endif
}

void write_new_file_atomic(
    const std::filesystem::path& path,
    const std::string& content
) {
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("immutable_evidence_already_exists");
    }
    if (!write_file_exclusive(path, content)) {
        throw std::runtime_error("immutable_evidence_already_exists");
    }
}

uint32_t sha256_rotr(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

class StreamingSha256 {
public:
    void update(const char* data, std::size_t size) {
        constexpr auto maximum =
            std::numeric_limits<std::uint64_t>::max() / 8u;
        if (size > maximum - total_bytes_) {
            throw std::runtime_error("sha256_input_too_large");
        }
        total_bytes_ += static_cast<std::uint64_t>(size);
        while (size > 0) {
            const auto take =
                std::min(size, buffer_.size() - buffer_size_);
            std::copy_n(
                reinterpret_cast<const std::uint8_t*>(data), take,
                buffer_.begin() +
                    static_cast<std::ptrdiff_t>(buffer_size_));
            buffer_size_ += take;
            data += take;
            size -= take;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_.data());
                buffer_size_ = 0;
            }
        }
    }

    void update(const std::string& value) {
        update(value.data(), value.size());
    }

    std::string final_hex() {
        const auto bit_length = total_bytes_ * 8u;
        buffer_[buffer_size_++] = 0x80u;
        if (buffer_size_ > 56u) {
            std::fill(
                buffer_.begin() +
                    static_cast<std::ptrdiff_t>(buffer_size_),
                buffer_.end(), 0u);
            transform(buffer_.data());
            buffer_size_ = 0;
        }
        std::fill(
            buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
            buffer_.begin() + 56, 0u);
        for (std::size_t index = 0; index < 8u; ++index) {
            buffer_[56u + index] = static_cast<std::uint8_t>(
                bit_length >> (56u - index * 8u));
        }
        transform(buffer_.data());
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (const auto part : hash_) {
            output << std::setw(8) << part;
        }
        return output.str();
    }

private:
    void transform(const std::uint8_t* block) {
        static constexpr std::array<uint32_t, 64> constants = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
            0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
            0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
            0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
            0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
            0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
            0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774u,0x34b0bcb5u,
            0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
            0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::array<uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16u; ++index) {
            const auto base = index * 4u;
            words[index] =
                (static_cast<uint32_t>(block[base]) << 24u) |
                (static_cast<uint32_t>(block[base + 1u]) << 16u) |
                (static_cast<uint32_t>(block[base + 2u]) << 8u) |
                static_cast<uint32_t>(block[base + 3u]);
        }
        for (std::size_t index = 16u; index < 64u; ++index) {
            const auto s0 =
                sha256_rotr(words[index - 15u], 7u) ^
                sha256_rotr(words[index - 15u], 18u) ^
                (words[index - 15u] >> 3u);
            const auto s1 =
                sha256_rotr(words[index - 2u], 17u) ^
                sha256_rotr(words[index - 2u], 19u) ^
                (words[index - 2u] >> 10u);
            words[index] =
                words[index - 16u] + s0 + words[index - 7u] + s1;
        }
        uint32_t a=hash_[0],b=hash_[1],c=hash_[2],d=hash_[3];
        uint32_t e=hash_[4],f=hash_[5],g=hash_[6],h=hash_[7];
        for (std::size_t index = 0; index < 64u; ++index) {
            const auto s1 =
                sha256_rotr(e,6u)^sha256_rotr(e,11u)^sha256_rotr(e,25u);
            const auto temporary1 =
                h+s1+((e&f)^((~e)&g))+constants[index]+words[index];
            const auto s0 =
                sha256_rotr(a,2u)^sha256_rotr(a,13u)^sha256_rotr(a,22u);
            const auto temporary2 = s0+((a&b)^(a&c)^(b&c));
            h=g; g=f; f=e; e=d+temporary1; d=c; c=b; b=a;
            a=temporary1+temporary2;
        }
        hash_[0]+=a; hash_[1]+=b; hash_[2]+=c; hash_[3]+=d;
        hash_[4]+=e; hash_[5]+=f; hash_[6]+=g; hash_[7]+=h;
    }

    std::array<std::uint32_t, 8> hash_ = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

std::string sha256_hex(const std::string& value) {
    StreamingSha256 hasher;
    hasher.update(value);
    return hasher.final_hex();
}

bool is_lowercase_sha256(const std::string& value) {
    return value.size() == 64u &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return (ch >= '0' && ch <= '9') ||
                      (ch >= 'a' && ch <= 'f');
           });
}

struct FileDigest {
    std::uintmax_t size = 0;
    std::string sha256;
};

FileDigest digest_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumSingleFileBytes) {
        throw std::runtime_error(
            error ? "file_stat_failed" : "single_file_limit_exceeded");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("file_read_failed");
    }
    StreamingSha256 hasher;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("file_read_failed");
    }
    return {size, hasher.final_hex()};
}

std::string json_string(const Json::Value& value, bool pretty) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = pretty ? "  " : "";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, value);
}

Json::Value parse_json(const std::string& content) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value value;
    std::string errors;
    std::istringstream input(content);
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("invalid_product_registration_json");
    }
    return value;
}

Json::Value string_array(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) {
        result.append(value);
    }
    return result;
}

std::optional<std::vector<std::string>> persisted_selector_array(
    const Json::Value& object,
    const char* member
) {
    if (!object.isObject()) {
        return std::nullopt;
    }
    if (!object.isMember(member)) {
        return std::vector<std::string>{};
    }
    const auto& value = object[member];
    if (!value.isArray()) {
        return std::nullopt;
    }

    std::vector<std::string> selectors;
    selectors.reserve(value.size());
    for (Json::ArrayIndex index = 0; index < value.size(); ++index) {
        if (!value[index].isString()) {
            return std::nullopt;
        }
        const auto selector = value[index].asString();
        if (selector.empty() ||
            ProjectConfig::normalize_product_selector(selector) != selector) {
            return std::nullopt;
        }
        selectors.push_back(selector);
    }
    auto normalized = selectors;
    sort_unique(normalized);
    return normalized == selectors
        ? std::optional<std::vector<std::string>>(std::move(selectors))
        : std::nullopt;
}

Json::Value nullable_string(const std::optional<std::string>& value) {
    return value ? Json::Value(*value) : Json::Value(Json::nullValue);
}

Json::Value request_json(const ProductRegistrationRequest& request) {
    Json::Value value(Json::objectValue);
    value["product"] = request.product;
    value["product_name"] = request.product_name;
    value["prefix"] = request.prefix;
    value["aliases"] = string_array(request.aliases);
    value["repo_bindings"] = string_array(request.repo_bindings);
    value["external_root_supplied"] = !request.external_root.empty();
    return value;
}

Json::Value limits_json(const ProductRegistrationLimits& limits) {
    Json::Value value(Json::objectValue);
    value["max_files"] = static_cast<Json::UInt64>(limits.max_files);
    value["max_bytes"] = static_cast<Json::UInt64>(limits.max_bytes);
    value["max_items"] = static_cast<Json::UInt64>(limits.max_items);
    return value;
}

Json::Value plan_json(
    const ProductRegistrationPlan& plan,
    bool include_hash
) {
    Json::Value value(Json::objectValue);
    value["schema"] = plan.schema;
    value["status"] = plan.status;
    value["request"] = request_json(plan.request);
    value["limits"] = limits_json(plan.limits);
    value["product"] = plan.product;
    value["product_name"] = plan.product_name;
    value["prefix"] = plan.prefix;
    value["aliases"] = string_array(plan.aliases);
    value["repo_bindings"] = string_array(plan.repo_bindings);
    value["config_ref"] = plan.config_ref;
    value["source_root_ref"] = plan.source_root_ref;
    value["canonical_destination_ref"] =
        plan.canonical_destination_ref;
    value["config_path_digest"] = plan.config_path_digest;
    value["external_root_path_digest"] =
        plan.external_root_path_digest;
    value["canonical_destination_path_digest"] =
        plan.canonical_destination_path_digest;
    value["source_revision"] = plan.source_revision;
    value["config_revision"] = plan.config_revision;
    value["proposed_config_revision"] = plan.proposed_config_revision;
    value["registry_revision"] = plan.registry_revision;
    Json::Value files(Json::arrayValue);
    for (const auto& file : plan.files) {
        Json::Value entry(Json::objectValue);
        entry["ref"] = file.ref;
        entry["kind"] = file.kind;
        entry["size"] = static_cast<Json::UInt64>(file.size);
        entry["sha256"] = file.sha256;
        files.append(entry);
    }
    value["files"] = files;
    Json::Value identities(Json::arrayValue);
    for (const auto& identity : plan.identities) {
        Json::Value entry(Json::objectValue);
        entry["id"] = identity.id;
        entry["uid"] = identity.uid;
        entry["source_ref"] = identity.source_ref;
        entry["source_sha256"] = identity.source_sha256;
        identities.append(entry);
    }
    value["identities"] = identities;
    value["safety_checks"] = string_array(plan.safety_checks);
    value["blockers"] = string_array(plan.blockers);
    value["warnings"] = string_array(plan.warnings);
    value["canonical_destination_absent"] =
        plan.canonical_destination_absent;
    value["dry_run"] = plan.dry_run;
    value["mutates_backlog"] = plan.mutates_backlog;
    value["external_root_read_only"] = plan.external_root_read_only;
    value["creates_canonical_destination"] =
        plan.creates_canonical_destination;
    value["plan_hash"] = include_hash ? plan.plan_hash : "";
    return value;
}

void add_blocker(ProductRegistrationPlan& plan, std::string blocker) {
    plan.blockers.push_back(std::move(blocker));
    plan.status = "blocked";
}

std::vector<std::string> registration_limit_blockers(
    const ProductRegistrationLimits& limits
) {
    std::vector<std::string> blockers;
    if (limits.max_files == 0u ||
        limits.max_files > kMaximumPublicEntryLimit) {
        blockers.emplace_back("invalid_max_files");
    }
    if (limits.max_bytes == 0u ||
        limits.max_bytes > kMaximumAggregateBytes) {
        blockers.emplace_back("invalid_max_bytes");
    }
    if (limits.max_items == 0u ||
        limits.max_items > kMaximumPublicEntryLimit) {
        blockers.emplace_back("invalid_max_items");
    }
    return blockers;
}

std::string source_file_ref(
    const std::string& product,
    const std::string& relative
) {
    return "registration:" + product + "/" + relative;
}

std::string first_component(const std::filesystem::path& relative) {
    const auto iterator = relative.begin();
    return iterator == relative.end() ? std::string{} :
        iterator->generic_string();
}

std::string classify_source_file(const std::filesystem::path& relative) {
    const auto generic = relative.generic_string();
    if (generic == "_config/config.toml") {
        return "source_local_config";
    }
    if (first_component(relative) == "items" &&
        relative.extension() == ".md" &&
        !relative.filename().generic_string().ends_with(".index.md")) {
        return "canonical_item";
    }
    if (relative.filename().generic_string().ends_with(".index.md")) {
        return "derived_item_index";
    }
    return "source_product_file";
}

bool is_derived_source_path(const std::filesystem::path& relative) {
    const auto top = first_component(relative);
    const auto filename = relative.filename().generic_string();
    return top == ".cache" || top == "views" || top == "_views" ||
           filename.ends_with(".index.md");
}

std::string escape_toml_basic_string(const std::string& value) {
    std::ostringstream escaped;
    for (const auto ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\b': escaped << "\\b"; break;
            case '\t': escaped << "\\t"; break;
            case '\n': escaped << "\\n"; break;
            case '\f': escaped << "\\f"; break;
            case '\r': escaped << "\\r"; break;
            default: escaped << ch; break;
        }
    }
    return escaped.str();
}

std::string toml_string_array(const std::vector<std::string>& values) {
    std::string serialized = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0u) {
            serialized += ", ";
        }
        serialized += '"';
        serialized += escape_toml_basic_string(values[index]);
        serialized += '"';
    }
    serialized += ']';
    return serialized;
}

std::string append_product_block(
    const std::string& before,
    const std::string& product,
    const std::string& name,
    const std::string& prefix,
    const std::vector<std::string>& aliases,
    const std::vector<std::string>& repo_bindings,
    const std::filesystem::path& external_root
) {
    std::string after = before;
    if (!after.empty() && after.back() != '\n') {
        after.push_back('\n');
    }
    if (!after.empty()) {
        after.push_back('\n');
    }
    after += "[products." + product + "]\n";
    after += "name = \"" + escape_toml_basic_string(name) + "\"\n";
    after += "prefix = \"" + escape_toml_basic_string(prefix) + "\"\n";
    after += "backlog_root = \"" +
             escape_toml_basic_string(external_root.generic_string()) +
             "\"\n";
    if (!aliases.empty()) {
        after += "aliases = " + toml_string_array(aliases) + "\n";
    }
    if (!repo_bindings.empty()) {
        after += "repo_bindings = " + toml_string_array(repo_bindings) +
                 "\n";
    }
    return after;
}

struct RegistryEntry {
    std::string product;
    std::string name;
    std::string prefix;
    std::string backlog_root;
    std::vector<std::string> aliases;
    std::vector<std::string> repo_bindings;
};

bool read_registry_string_array(
    const toml::table& table,
    std::string_view key,
    std::vector<std::string>& values
) {
    const auto* node = table.get(key);
    if (!node) {
        values.clear();
        return true;
    }
    const auto* array = node->as_array();
    if (!array) {
        return false;
    }
    values.clear();
    values.reserve(array->size());
    for (const auto& element : *array) {
        const auto value = element.value<std::string>();
        if (!value) {
            return false;
        }
        values.push_back(*value);
    }
    return true;
}

std::optional<std::vector<RegistryEntry>> parse_registry(
    const std::string& content
) {
    try {
        const auto document = toml::parse(content);
        std::vector<RegistryEntry> entries;
        const auto* products = document["products"].as_table();
        if (!products) {
            return entries;
        }
        for (const auto& [key, node] : *products) {
            const auto* table = node.as_table();
            if (!table) {
                return std::nullopt;
            }
            const auto name = (*table)["name"].value<std::string>();
            const auto prefix = (*table)["prefix"].value<std::string>();
            const auto root = (*table)["backlog_root"].value<std::string>();
            if (!name || !prefix || !root || root->empty()) {
                return std::nullopt;
            }
            std::vector<std::string> aliases;
            std::vector<std::string> repo_bindings;
            if (!read_registry_string_array(*table, "aliases", aliases) ||
                !read_registry_string_array(
                    *table, "repo_bindings", repo_bindings)) {
                return std::nullopt;
            }
            entries.push_back(RegistryEntry{
                .product = std::string(key.str()),
                .name = *name,
                .prefix = *prefix,
                .backlog_root = *root,
                .aliases = std::move(aliases),
                .repo_bindings = std::move(repo_bindings),
            });
        }
        std::sort(
            entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                return left.product < right.product;
            });
        return entries;
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
}

std::optional<RegistryEntry> find_registry_entry(
    const std::string& content,
    const std::string& product
) {
    const auto entries = parse_registry(content);
    if (!entries) {
        return std::nullopt;
    }
    const auto found = std::find_if(
        entries->begin(), entries->end(), [&](const auto& entry) {
            return entry.product == product;
        });
    return found == entries->end() ? std::nullopt :
        std::optional<RegistryEntry>(*found);
}

std::filesystem::path resolve_registered_root(
    const std::filesystem::path& config_root,
    const std::string& value
) {
    const std::filesystem::path configured(value);
    return existing_path_identity(
               configured.is_absolute() ? configured : config_root / configured)
        .final_path;
}

bool config_registration_matches(
    const std::string& content,
    const std::filesystem::path& config_root,
    const ProductRegistrationRequest& request,
    const std::filesystem::path& external_root
) {
    const auto entry = find_registry_entry(content, request.product);
    return entry && entry->name == request.product_name &&
           entry->prefix == request.prefix &&
           entry->aliases == request.aliases &&
           entry->repo_bindings == request.repo_bindings &&
           resolve_registered_root(config_root, entry->backlog_root) ==
               external_root;
}

bool source_local_config_matches(
    const std::string& content,
    const ProductRegistrationRequest& request
) {
    try {
        const auto document = toml::parse(content);
        const auto* product = document["product"].as_table();
        if (!product) {
            return false;
        }
        const auto name = (*product)["name"].value<std::string>();
        const auto prefix = (*product)["prefix"].value<std::string>();
        return name && prefix && *name == request.product_name &&
               *prefix == request.prefix;
    } catch (const toml::parse_error&) {
        return false;
    }
}

struct ManifestEntry {
    std::string relative;
    std::string kind;
    std::uintmax_t size = 0;
    std::string sha256;
};

struct ParsedIdentity {
    BacklogItem item;
    std::string relative;
    std::string sha256;
};

struct PreparedRegistration {
    ProductRegistrationPlan plan;
    std::filesystem::path config_root;
    std::filesystem::path config_path;
    std::filesystem::path products_root;
    std::filesystem::path external_root;
    std::filesystem::path canonical_destination;
    std::string config_before;
    std::string config_after;
    std::vector<ManifestEntry> manifest;
    std::vector<ParsedIdentity> parsed_identities;
    std::size_t source_metadata_attempts = 0;
    std::uintmax_t frontmatter_bytes_read = 0;
};

std::optional<std::size_t> frontmatter_read_cap(
    const PreparedRegistration& prepared
) {
    if (prepared.frontmatter_bytes_read >= prepared.plan.limits.max_bytes) {
        return std::nullopt;
    }
    const auto remaining =
        prepared.plan.limits.max_bytes - prepared.frontmatter_bytes_read;
    return static_cast<std::size_t>(std::min<std::uintmax_t>(
        kMaximumFrontmatterBytes, remaining));
}

void charge_frontmatter_bytes(
    PreparedRegistration& prepared,
    std::size_t bytes_read
) {
    const auto remaining =
        prepared.plan.limits.max_bytes - prepared.frontmatter_bytes_read;
    prepared.frontmatter_bytes_read +=
        std::min<std::uintmax_t>(bytes_read, remaining);
}

std::string item_type_code(ItemType type) {
    switch (type) {
        case ItemType::Initiative: return "INIT";
        case ItemType::Epic: return "EPIC";
        case ItemType::Feature: return "FTR";
        case ItemType::UserStory: return "USR";
        case ItemType::Task: return "TSK";
        case ItemType::SubTask: return "SUBTSK";
        case ItemType::Bug: return "BUG";
        case ItemType::Issue: return "ISS";
    }
    return {};
}

std::string item_type_directory(ItemType type) {
    switch (type) {
        case ItemType::Initiative: return "initiative";
        case ItemType::Epic: return "epic";
        case ItemType::Feature: return "feature";
        case ItemType::UserStory: return "userstory";
        case ItemType::Task: return "task";
        case ItemType::SubTask: return "subtask";
        case ItemType::Bug: return "bug";
        case ItemType::Issue: return "issue";
    }
    return {};
}

bool is_strict_uuid_v7(const std::string& value) {
    static const std::regex grammar(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)");
    return std::regex_match(value, grammar);
}

bool strict_item_filename_matches(
    const std::filesystem::path& relative,
    const BacklogItem& item,
    const std::string& requested_prefix
) {
    static const std::regex id_grammar(
        R"(^([A-Z][A-Z0-9]{1,15})-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-([0-9]{4})$)");
    std::smatch match;
    if (!std::regex_match(item.id, match, id_grammar) ||
        match[1].str() != requested_prefix ||
        match[2].str() != item_type_code(item.type)) {
        return false;
    }
    const auto filename = relative.filename().generic_string();
    const auto required_prefix = item.id + "_";
    if (!filename.starts_with(required_prefix) ||
        !filename.ends_with(".md")) {
        return false;
    }
    const auto slug = filename.substr(
        required_prefix.size(),
        filename.size() - required_prefix.size() - 3u);
    static const std::regex slug_grammar(
        R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)");
    if (!std::regex_match(slug, slug_grammar)) {
        return false;
    }
    std::vector<std::string> components;
    for (const auto& component : relative) {
        components.push_back(component.generic_string());
    }
    if (components.size() != 4u || components[0] != "items" ||
        components[1] != item_type_directory(item.type)) {
        return false;
    }
    const auto number = std::stoi(match[3].str());
    std::ostringstream bucket;
    bucket << std::setfill('0') << std::setw(4) << (number / 100) * 100;
    return components[2] == bucket.str();
}

void compute_source_revision(PreparedRegistration& prepared) {
    std::sort(
        prepared.manifest.begin(), prepared.manifest.end(),
        [](const auto& left, const auto& right) {
            return left.relative < right.relative;
        });
    StreamingSha256 revision;
    for (const auto& file : prepared.manifest) {
        revision.update(file.relative);
        revision.update("\0", 1u);
        revision.update(std::to_string(file.size));
        revision.update("\0", 1u);
        revision.update(file.sha256);
        revision.update("\n", 1u);
    }
    prepared.plan.source_revision = revision.final_hex();
}

void inventory_source(PreparedRegistration& prepared) {
    std::uintmax_t total_bytes = 0;
    std::size_t item_count = 0;
    PathEntryBudget entry_budget(prepared.plan.limits.max_files);
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        prepared.external_root,
        std::filesystem::directory_options::none,
        iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    if (iterator_error) {
        add_blocker(prepared.plan, "source_inventory_open_failed");
        compute_source_revision(prepared);
        return;
    }
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            add_blocker(prepared.plan, "source_inventory_read_failed");
            break;
        }
        const auto& entry = *iterator;
        if (!entry_budget.charge()) {
            add_blocker(
                prepared.plan, "source_path_entry_limit_exceeded");
            break;
        }
        if (path_entry_is_reparse(entry.path())) {
            add_blocker(
                prepared.plan,
                "source_symlink_or_reparse_not_supported");
            std::error_code directory_error;
            if (entry.is_directory(directory_error) && !directory_error) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        const auto relative =
            entry.path().lexically_relative(prepared.external_root);
        if (relative.empty() || relative.is_absolute() ||
            first_component(relative) == "..") {
            add_blocker(prepared.plan, "source_path_escape_detected");
            continue;
        }
        std::error_code directory_error;
        if (entry.is_directory(directory_error) && !directory_error) {
            if (is_derived_source_path(relative)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        std::error_code regular_error;
        if (!entry.is_regular_file(regular_error) || regular_error) {
            continue;
        }
        if (is_derived_source_path(relative)) {
            continue;
        }
        if (prepared.manifest.size() >= prepared.plan.limits.max_files) {
            add_blocker(prepared.plan, "source_file_limit_exceeded");
            break;
        }
        const auto kind = classify_source_file(relative);
        if (kind == "canonical_item" &&
            ++item_count > prepared.plan.limits.max_items) {
            add_blocker(prepared.plan, "source_item_limit_exceeded");
            break;
        }
        std::error_code size_error;
        const auto file_size = entry.file_size(size_error);
        if (size_error) {
            add_blocker(prepared.plan, "source_file_unreadable");
            continue;
        }
        if (file_size > prepared.plan.limits.max_bytes - total_bytes) {
            add_blocker(prepared.plan, "source_byte_limit_exceeded");
            break;
        }
        FileDigest digest;
        try {
            digest = digest_file(entry.path());
        } catch (const std::exception&) {
            add_blocker(
                prepared.plan,
                "source_file_unreadable:" +
                    source_file_ref(
                        prepared.plan.product,
                        relative.generic_string()));
            continue;
        }
        if (digest.size != file_size) {
            add_blocker(prepared.plan, "source_file_changed_during_inventory");
            break;
        }
        total_bytes += digest.size;
        prepared.manifest.push_back({
            relative.generic_string(), kind, digest.size, digest.sha256,
        });
        prepared.plan.files.push_back(ProductRegistrationFile{
            source_file_ref(
                prepared.plan.product, relative.generic_string()),
            kind,
            digest.size,
            digest.sha256,
        });
    }
    compute_source_revision(prepared);
}

std::optional<ManifestEntry> manifest_entry(
    const PreparedRegistration& prepared,
    const std::filesystem::path& path
) {
    const auto relative =
        path.lexically_relative(prepared.external_root).generic_string();
    const auto found = std::lower_bound(
        prepared.manifest.begin(), prepared.manifest.end(), relative,
        [](const auto& entry, const auto& value) {
            return entry.relative < value;
        });
    if (found == prepared.manifest.end() || found->relative != relative) {
        return std::nullopt;
    }
    return *found;
}

void collect_source_identities(PreparedRegistration& prepared) {
    CanonicalStore store(prepared.external_root);
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> uids;
    std::size_t item_count = 0;
    const auto items_root = prepared.external_root / "items";
    if (!std::filesystem::is_directory(items_root)) {
        add_blocker(prepared.plan, "source_items_missing");
        return;
    }
    for (const auto& file : prepared.manifest) {
        const auto relative = std::filesystem::path(file.relative);
        if (file.kind != "canonical_item") {
            if (first_component(relative) == "items" &&
                file.kind != "derived_item_index") {
                add_blocker(
                    prepared.plan, "invalid_source_item_filename");
            }
            continue;
        }
        if (++item_count > prepared.plan.limits.max_items) {
            add_blocker(prepared.plan, "source_item_limit_exceeded");
            return;
        }
        const auto maximum_bytes = frontmatter_read_cap(prepared);
        if (!maximum_bytes) {
            add_blocker(
                prepared.plan, "source_frontmatter_byte_limit_exceeded");
            return;
        }
        std::size_t bytes_read = 0;
        std::optional<BacklogItem> parsed_item;
        try {
            ++prepared.source_metadata_attempts;
            parsed_item = store.read_metadata_bounded(
                prepared.external_root / relative,
                *maximum_bytes,
                &bytes_read);
        } catch (const ParseError& error) {
            charge_frontmatter_bytes(prepared, bytes_read);
            if (error.details == "frontmatter_byte_limit_exceeded") {
                add_blocker(
                    prepared.plan,
                    "source_frontmatter_byte_limit_exceeded");
                return;
            }
            add_blocker(prepared.plan, "malformed_source_item");
            continue;
        } catch (const std::exception&) {
            charge_frontmatter_bytes(prepared, bytes_read);
            add_blocker(prepared.plan, "malformed_source_item");
            continue;
        }
        charge_frontmatter_bytes(prepared, bytes_read);
        auto item = std::move(*parsed_item);
        if (!strict_item_filename_matches(
                relative, item, prepared.plan.prefix)) {
            add_blocker(
                prepared.plan,
                item.id.starts_with(prepared.plan.prefix + "-")
                    ? "source_item_filename_mismatch"
                    : "source_item_prefix_mismatch");
        }
        if (!ids.insert(item.id).second) {
            add_blocker(
                prepared.plan,
                "duplicate_source_display_id:" + item.id);
        }
        if (!is_strict_uuid_v7(item.uid)) {
            add_blocker(
                prepared.plan,
                "invalid_source_uuid_v7:" + item.id);
        } else if (!uids.insert(item.uid).second) {
            add_blocker(
                prepared.plan,
                "duplicate_source_uid:" + item.uid);
        }
        prepared.plan.identities.push_back(
            ProductRegistrationIdentity{
                item.id,
                item.uid,
                source_file_ref(
                    prepared.plan.product, relative.generic_string()),
                file.sha256,
            });
        prepared.parsed_identities.push_back(
            {std::move(item), relative.generic_string(), file.sha256});
    }
    if (prepared.plan.identities.empty()) {
        add_blocker(prepared.plan, "source_items_empty");
    }
}

std::vector<std::filesystem::path> registry_item_paths(
    const std::filesystem::path& root,
    ProductRegistrationPlan& plan,
    RegistryScanBudget& budget
) {
    std::vector<std::filesystem::path> paths;
    const auto items = root / "items";
    if (!std::filesystem::exists(items)) {
        return paths;
    }
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        items, std::filesystem::directory_options::none, iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    if (iterator_error) {
        add_blocker(plan, "registry_inventory_open_failed");
        return paths;
    }
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            add_blocker(plan, "registry_inventory_read_failed");
            break;
        }
        if (!budget.paths.charge()) {
            add_blocker(plan, "registry_path_entry_limit_exceeded");
            budget.path_limit_exceeded = true;
            break;
        }
        if (path_entry_is_reparse(iterator->path())) {
            add_blocker(plan, "registry_symlink_or_reparse_not_supported");
            std::error_code directory_error;
            if (iterator->is_directory(directory_error) && !directory_error) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        std::error_code regular_error;
        if (iterator->is_regular_file(regular_error) && !regular_error &&
            iterator->path().extension() == ".md" &&
            !iterator->path().filename().generic_string().ends_with(
                ".index.md")) {
            if (budget.remaining_items == 0u) {
                add_blocker(plan, "registry_item_scan_limit_exceeded");
                budget.item_limit_exceeded = true;
                break;
            }
            --budget.remaining_items;
            paths.push_back(iterator->path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

ProductDefinition registry_product_definition(const RegistryEntry& entry) {
    ProductDefinition definition;
    definition.name = entry.name;
    definition.prefix = entry.prefix;
    definition.backlog_root = entry.backlog_root;
    definition.aliases = entry.aliases;
    definition.repo_bindings = entry.repo_bindings;
    return definition;
}

void add_selector_collision_blockers(
    PreparedRegistration& prepared,
    const std::vector<RegistryEntry>& registry
) {
    ProjectConfig prospective;
    for (const auto& entry : registry) {
        prospective.products.emplace(
            entry.product, registry_product_definition(entry));
    }
    if (!prospective.products.contains(prepared.plan.product)) {
        ProductDefinition proposed;
        proposed.name = prepared.plan.product_name;
        proposed.prefix = prepared.plan.prefix;
        proposed.backlog_root = prepared.external_root.generic_string();
        proposed.aliases = prepared.plan.aliases;
        proposed.repo_bindings = prepared.plan.repo_bindings;
        prospective.products.emplace(
            prepared.plan.product, std::move(proposed));
    }
    for (const auto& collision : prospective.find_selector_collisions()) {
        const auto proposed_product_claims_selector = std::any_of(
            collision.claims.begin(),
            collision.claims.end(),
            [&](const auto& claim) {
                return claim.canonical_slug == prepared.plan.product;
            });
        if (!proposed_product_claims_selector) {
            continue;
        }
        add_blocker(
            prepared.plan,
            "product_selector_collision:" + collision.normalized_selector);
    }
}

void update_registry_selector_revision(
    StreamingSha256& revision,
    std::string_view kind,
    const std::vector<std::string>& values
) {
    revision.update(kind.data(), kind.size());
    revision.update("\0", 1u);
    for (const auto& value : values) {
        revision.update(value);
        revision.update("\0", 1u);
    }
    revision.update("\n", 1u);
}

void scan_registry(
    PreparedRegistration& prepared,
    const std::vector<RegistryEntry>& registry
) {
    std::unordered_set<std::string> source_ids;
    std::unordered_set<std::string> source_uids;
    for (const auto& identity : prepared.plan.identities) {
        source_ids.insert(identity.id);
        source_uids.insert(identity.uid);
    }
    std::unordered_map<std::string, std::string> known_ids;
    std::unordered_map<std::string, std::string> known_uids;
    std::unordered_map<std::string, std::string> prefixes;
    StreamingSha256 revision;
    RegistryScanBudget scan_budget(
        prepared.plan.limits.max_items,
        prepared.source_metadata_attempts);

    add_selector_collision_blockers(prepared, registry);

    for (const auto& entry : registry) {
        revision.update(entry.product);
        revision.update("\0", 1u);
        revision.update(entry.name);
        revision.update("\0", 1u);
        revision.update(entry.prefix);
        revision.update("\0", 1u);
        revision.update(entry.backlog_root);
        revision.update("\n", 1u);
        update_registry_selector_revision(
            revision, "aliases", entry.aliases);
        update_registry_selector_revision(
            revision, "repo_bindings", entry.repo_bindings);

        if (entry.product == prepared.plan.product) {
            add_blocker(prepared.plan, "product_already_registered");
        } else if (lowercase_ascii(entry.product) ==
                   lowercase_ascii(prepared.plan.product)) {
            add_blocker(prepared.plan, "product_case_fold_collision");
        }
        const auto normalized_prefix = lowercase_ascii(entry.prefix);
        if (normalized_prefix == lowercase_ascii(prepared.plan.prefix)) {
            add_blocker(prepared.plan, "prefix_collision:" + entry.product);
        }
        const auto prior_prefix = prefixes.find(normalized_prefix);
        if (!normalized_prefix.empty() && prior_prefix != prefixes.end()) {
            if (prior_prefix->second == prepared.plan.product ||
                entry.product == prepared.plan.product) {
                add_blocker(
                    prepared.plan,
                    "existing_prefix_collision:" + prior_prefix->second + ":" +
                        entry.product);
            }
        } else if (!normalized_prefix.empty()) {
            prefixes.emplace(normalized_prefix, entry.product);
        }

        std::filesystem::path root;
        try {
            root = resolve_registered_root(
                prepared.config_root, entry.backlog_root);
        } catch (const std::exception&) {
            add_blocker(
                prepared.plan,
                "registered_root_unresolved:" + entry.product);
            continue;
        }
        if (path_equal(root, prepared.external_root)) {
            add_blocker(
                prepared.plan,
                "external_root_collision:" + entry.product);
        }
        if (is_within_or_equal(root, prepared.external_root) ||
            is_within_or_equal(prepared.external_root, root)) {
            add_blocker(
                prepared.plan,
                "external_root_overlaps_registered_root:" + entry.product);
        }
        if (path_equal(root, prepared.canonical_destination)) {
            add_blocker(
                prepared.plan,
                "canonical_destination_registered_to_other_product:" +
                    entry.product);
        }
        if (!std::filesystem::is_directory(root) ||
            existing_path_has_reparse_component(root)) {
            add_blocker(
                prepared.plan,
                "registered_root_missing_or_unsafe:" + entry.product);
            continue;
        }

        CanonicalStore store(root);
        const auto paths = registry_item_paths(
            root, prepared.plan, scan_budget);
        if (scan_budget.exhausted()) {
            prepared.plan.registry_revision = revision.final_hex();
            return;
        }
        for (const auto& path : paths) {
            const auto maximum_bytes = frontmatter_read_cap(prepared);
            if (!maximum_bytes) {
                add_blocker(
                    prepared.plan,
                    "registry_frontmatter_byte_limit_exceeded");
                prepared.plan.registry_revision = revision.final_hex();
                return;
            }
            std::size_t bytes_read = 0;
            std::optional<BacklogItem> parsed_item;
            try {
                parsed_item = store.read_metadata_bounded(
                    path, *maximum_bytes, &bytes_read);
            } catch (const ParseError& error) {
                charge_frontmatter_bytes(prepared, bytes_read);
                if (error.details == "frontmatter_byte_limit_exceeded") {
                    add_blocker(
                        prepared.plan,
                        "registry_frontmatter_byte_limit_exceeded");
                    prepared.plan.registry_revision = revision.final_hex();
                    return;
                }
                add_blocker(
                    prepared.plan,
                    "registry_item_unreadable:" + entry.product);
                continue;
            } catch (const std::exception&) {
                charge_frontmatter_bytes(prepared, bytes_read);
                add_blocker(
                    prepared.plan,
                    "registry_item_unreadable:" + entry.product);
                continue;
            }
            charge_frontmatter_bytes(prepared, bytes_read);
            const auto& item = *parsed_item;
            if (!is_strict_uuid_v7(item.uid)) {
                add_blocker(
                    prepared.plan,
                    "registry_item_invalid_uuid_v7:" + entry.product);
            }
            revision.update(entry.product);
            revision.update("\0", 1u);
            revision.update(item.id);
            revision.update("\0", 1u);
            revision.update(item.uid);
            revision.update("\n", 1u);
            if (source_ids.contains(item.id)) {
                add_blocker(
                    prepared.plan,
                    "display_id_collision:" + item.id + ":" +
                        entry.product);
            }
            if (source_uids.contains(item.uid)) {
                add_blocker(
                    prepared.plan,
                    "uid_collision:" + item.uid + ":" + entry.product);
            }
            const auto [id_position, id_inserted] =
                known_ids.emplace(item.id, entry.product);
            if (!id_inserted) {
                add_blocker(
                    prepared.plan,
                    "registry_display_id_collision:" + item.id + ":" +
                        id_position->second + ":" + entry.product);
            }
            const auto [uid_position, uid_inserted] =
                known_uids.emplace(item.uid, entry.product);
            if (!uid_inserted) {
                add_blocker(
                    prepared.plan,
                    "registry_uid_collision:" + item.uid + ":" +
                        uid_position->second + ":" + entry.product);
            }
        }
    }
    prepared.plan.registry_revision = revision.final_hex();
}

void finalize_plan(PreparedRegistration& prepared) {
    std::sort(
        prepared.plan.files.begin(), prepared.plan.files.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.ref, left.kind) <
                   std::tie(right.ref, right.kind);
        });
    std::sort(
        prepared.plan.identities.begin(), prepared.plan.identities.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.id, left.uid, left.source_ref) <
                   std::tie(right.id, right.uid, right.source_ref);
        });
    sort_unique(prepared.plan.safety_checks);
    sort_unique(prepared.plan.blockers);
    sort_unique(prepared.plan.warnings);
    prepared.plan.status =
        prepared.plan.blockers.empty() ? "ready" : "blocked";
    prepared.plan.plan_hash = sha256_hex(
        json_string(plan_json(prepared.plan, false), false));
}

PreparedRegistration build_prepared(
    const kano::backlog_ops::ProductRegistrationOps::PlanOptions& options
) {
    PreparedRegistration prepared;
    prepared.plan.request = options.request;
    prepared.plan.limits = options.limits;
    prepared.plan.product = options.request.product;
    prepared.plan.product_name = options.request.product_name;
    prepared.plan.prefix = options.request.prefix;
    bool has_empty_alias = false;
    bool has_empty_repo_binding = false;
    prepared.plan.aliases = normalize_selector_list(
        options.request.aliases, has_empty_alias);
    prepared.plan.repo_bindings = normalize_selector_list(
        options.request.repo_bindings, has_empty_repo_binding);
    prepared.plan.request.aliases = prepared.plan.aliases;
    prepared.plan.request.repo_bindings = prepared.plan.repo_bindings;
    if (has_empty_alias) {
        add_blocker(prepared.plan, "invalid_alias_selector");
    }
    if (has_empty_repo_binding) {
        add_blocker(prepared.plan, "invalid_repo_binding_selector");
    }
    prepared.plan.config_ref =
        "project-config:.kano/backlog_config.toml";
    prepared.plan.source_root_ref =
        "registration:" + options.request.product + ":external-root";
    prepared.plan.canonical_destination_ref =
        "product:" + options.request.product + ":shared-root";
    prepared.plan.safety_checks = {
        "canonical_destination_must_remain_absent",
        "config_only_atomic_publish",
        "external_root_is_read_only",
        "no_manual_rollback_surface",
        "source_and_shared_paths_are_reparse_safe",
    };
    prepared.plan.warnings = {
        "registration_precedes_product_relocation",
        "recovery_requires_repeated_exact_apply",
    };

    if (!is_valid_product_slug(options.request.product)) {
        add_blocker(prepared.plan, "invalid_product_slug");
    }
    if (!is_valid_product_name(options.request.product_name)) {
        add_blocker(prepared.plan, "invalid_product_name");
    }
    if (!is_valid_prefix(options.request.prefix)) {
        add_blocker(prepared.plan, "invalid_prefix");
    }
    const auto limit_blockers = registration_limit_blockers(options.limits);
    for (const auto& blocker : limit_blockers) {
        add_blocker(prepared.plan, blocker);
    }
    if (!limit_blockers.empty()) {
        finalize_plan(prepared);
        return prepared;
    }
    if (options.backlog_root.empty() ||
        !options.backlog_root.is_absolute()) {
        add_blocker(prepared.plan, "backlog_root_must_be_absolute");
    }
    if (options.request.external_root.empty() ||
        !options.request.external_root.is_absolute()) {
        add_blocker(prepared.plan, "external_root_must_be_absolute");
    }
    if (!options.backlog_root.empty() && options.backlog_root.is_absolute()) {
        if (!std::filesystem::is_directory(options.backlog_root)) {
            add_blocker(prepared.plan, "shared_backlog_config_not_found");
        } else if (existing_path_has_reparse_component(options.backlog_root)) {
            add_blocker(prepared.plan, "shared_path_reparse_not_supported");
        } else {
            try {
                const auto root_identity = existing_path_identity(
                    options.backlog_root);
                if (!root_identity.is_directory) {
                    throw std::runtime_error("shared_root_not_directory");
                }
                prepared.config_root = root_identity.final_path;

                const auto kano_path = prepared.config_root / ".kano";
                const auto products_path = prepared.config_root / "products";
                if (!std::filesystem::is_directory(kano_path)) {
                    add_blocker(prepared.plan, "shared_kano_root_not_found");
                } else if (existing_path_has_reparse_component(kano_path)) {
                    add_blocker(
                        prepared.plan, "shared_path_reparse_not_supported");
                } else {
                    const auto kano_identity = existing_path_identity(kano_path);
                    if (!kano_identity.is_directory ||
                        !is_direct_child(
                            kano_identity.final_path, prepared.config_root)) {
                        add_blocker(
                            prepared.plan,
                            "shared_kano_root_authority_mismatch");
                    }
                    const auto config_path = kano_path / "backlog_config.toml";
                    if (std::filesystem::is_regular_file(config_path) &&
                        !existing_path_has_reparse_component(config_path)) {
                        const auto config_identity = existing_path_identity(
                            config_path);
                        if (!config_identity.is_directory &&
                            is_direct_child(
                                config_identity.final_path,
                                kano_identity.final_path)) {
                            prepared.config_path = config_identity.final_path;
                            prepared.plan.config_path_digest = sha256_hex(
                                prepared.config_path.generic_string());
                        } else {
                            add_blocker(
                                prepared.plan,
                                "shared_config_authority_mismatch");
                        }
                    } else {
                        add_blocker(
                            prepared.plan,
                            "shared_backlog_config_not_found");
                    }
                }

                if (!std::filesystem::is_directory(products_path)) {
                    add_blocker(
                        prepared.plan, "shared_products_root_not_found");
                } else if (existing_path_has_reparse_component(products_path)) {
                    add_blocker(
                        prepared.plan, "shared_path_reparse_not_supported");
                } else {
                    const auto products_identity = existing_path_identity(
                        products_path);
                    if (!products_identity.is_directory ||
                        !is_direct_child(
                            products_identity.final_path,
                            prepared.config_root)) {
                        add_blocker(
                            prepared.plan,
                            "shared_products_root_authority_mismatch");
                    } else {
                        prepared.products_root = products_identity.final_path;
                        prepared.canonical_destination =
                            (prepared.products_root /
                             options.request.product)
                                .lexically_normal();
                        if (!is_direct_child(
                                prepared.canonical_destination,
                                prepared.products_root)) {
                            add_blocker(
                                prepared.plan,
                                "canonical_destination_authority_mismatch");
                        }
                        prepared.plan.canonical_destination_path_digest =
                            sha256_hex(
                                prepared.canonical_destination.generic_string());
                    }
                }
            } catch (const std::exception&) {
                add_blocker(
                    prepared.plan, "shared_path_identity_validation_failed");
            }
        }
    }
    if (!options.request.external_root.empty() &&
        options.request.external_root.is_absolute()) {
        if (!std::filesystem::is_directory(options.request.external_root)) {
            add_blocker(prepared.plan, "source_root_not_found");
        } else if (existing_path_has_reparse_component(
                       options.request.external_root)) {
            add_blocker(prepared.plan, "source_root_reparse_not_supported");
        } else {
            try {
                const auto source_identity = existing_path_identity(
                    options.request.external_root);
                if (!source_identity.is_directory) {
                    throw std::runtime_error("source_root_not_directory");
                }
                prepared.external_root = source_identity.final_path;
                prepared.plan.external_root_path_digest =
                    sha256_hex(prepared.external_root.generic_string());
            } catch (const std::exception&) {
                add_blocker(
                    prepared.plan, "source_root_identity_validation_failed");
            }
        }
    }

    if (prepared.config_root.empty() ||
        !std::filesystem::is_directory(prepared.config_root) ||
        !std::filesystem::is_regular_file(prepared.config_path)) {
        add_blocker(prepared.plan, "shared_backlog_config_not_found");
        finalize_plan(prepared);
        return prepared;
    }
    try {
        prepared.config_before = read_file(prepared.config_path);
        prepared.plan.config_revision = sha256_hex(prepared.config_before);
    } catch (const std::exception&) {
        add_blocker(prepared.plan, "shared_backlog_config_unreadable");
        finalize_plan(prepared);
        return prepared;
    }
    const auto registry = parse_registry(prepared.config_before);
    if (!registry) {
        add_blocker(prepared.plan, "shared_config_malformed");
        finalize_plan(prepared);
        return prepared;
    }

    if (prepared.external_root.empty() ||
        !std::filesystem::is_directory(prepared.external_root)) {
        add_blocker(prepared.plan, "source_root_not_found");
    }
    if (!prepared.config_root.empty() && !prepared.external_root.empty() &&
        (is_within_or_equal(prepared.external_root, prepared.config_root) ||
         is_within_or_equal(prepared.config_root, prepared.external_root))) {
        add_blocker(prepared.plan, "shared_and_external_roots_overlap");
    }
    if (!prepared.canonical_destination.empty()) {
        prepared.plan.canonical_destination_absent =
            !std::filesystem::exists(prepared.canonical_destination);
        if (!prepared.plan.canonical_destination_absent) {
            add_blocker(
                prepared.plan,
                "canonical_destination_must_be_absent");
        }
        if (!is_direct_child(
                prepared.canonical_destination, prepared.products_root) ||
            existing_path_has_reparse_component(prepared.products_root)) {
            add_blocker(
                prepared.plan, "shared_path_reparse_not_supported");
        }
    }

    if (std::filesystem::is_directory(prepared.external_root)) {
        inventory_source(prepared);
        collect_source_identities(prepared);
        const auto local_config =
            prepared.external_root / "_config" / "config.toml";
        if (std::filesystem::exists(local_config)) {
            try {
                if (!source_local_config_matches(
                        read_file(local_config), options.request)) {
                    add_blocker(
                        prepared.plan, "source_local_config_mismatch");
                }
            } catch (const std::exception&) {
                add_blocker(
                    prepared.plan, "source_local_config_mismatch");
            }
        }
    } else {
        compute_source_revision(prepared);
    }

    scan_registry(prepared, *registry);
    prepared.config_after = append_product_block(
        prepared.config_before,
        options.request.product,
        options.request.product_name,
        options.request.prefix,
        prepared.plan.aliases,
        prepared.plan.repo_bindings,
        prepared.external_root);
    prepared.plan.proposed_config_revision =
        sha256_hex(prepared.config_after);
    if (!config_registration_matches(
            prepared.config_after,
            prepared.config_root,
            prepared.plan.request,
            prepared.external_root)) {
        add_blocker(prepared.plan, "prospective_config_resolution_failed");
    }
    finalize_plan(prepared);
    return prepared;
}

struct CachedPlan {
    ProductRegistrationPlan plan;
};

std::mutex& plan_cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, CachedPlan>& plan_cache() {
    static std::map<std::string, CachedPlan> cache;
    return cache;
}

std::deque<std::string>& plan_cache_order() {
    static std::deque<std::string> order;
    return order;
}

void cache_plan(const ProductRegistrationPlan& plan) {
    std::lock_guard lock(plan_cache_mutex());
    auto& cache = plan_cache();
    auto& order = plan_cache_order();
    if (!cache.contains(plan.plan_hash)) {
        order.push_back(plan.plan_hash);
    }
    cache[plan.plan_hash] = CachedPlan{plan};
    while (order.size() > kMaximumPlanCacheEntries) {
        cache.erase(order.front());
        order.pop_front();
    }
}

std::optional<CachedPlan> cached_plan(const std::string& hash) {
    std::lock_guard lock(plan_cache_mutex());
    const auto found = plan_cache().find(hash);
    return found == plan_cache().end() ? std::nullopt :
        std::optional<CachedPlan>(found->second);
}

std::filesystem::path transaction_root(
    const std::filesystem::path& config_root,
    const std::string& plan_hash
) {
    if (!is_lowercase_sha256(plan_hash)) {
        throw std::runtime_error("invalid_product_registration_plan_hash");
    }
    return config_root / ".kano" / "cache" /
           "product-registrations" / plan_hash;
}

std::string receipt_ref(const std::string& plan_hash) {
    return "project-cache:product-registrations/" + plan_hash +
           "/receipt.json";
}

struct EvidencePaths {
    std::filesystem::path config_root;
    std::filesystem::path kano_root;
    std::filesystem::path products_root;
    std::filesystem::path cache_root;
    std::filesystem::path registrations_root;
    std::filesystem::path config_path;
    std::filesystem::path transaction;
    std::filesystem::path before;
    std::filesystem::path after;
    std::filesystem::path receipt;
    std::filesystem::path journal;
};

EvidencePaths evidence_paths(
    const kano::backlog_ops::ProductRegistrationOps::RecoveryOptions& options
) {
    if (options.backlog_root.empty() ||
        !options.backlog_root.is_absolute()) {
        throw std::runtime_error("backlog_root_must_be_absolute");
    }
    EvidencePaths paths;
    const auto root_identity = existing_path_identity(options.backlog_root);
    if (!root_identity.is_directory) {
        throw std::runtime_error("shared_backlog_root_not_directory");
    }
    paths.config_root = root_identity.final_path;

    const auto kano_identity = existing_path_identity(
        paths.config_root / ".kano");
    const auto products_identity = existing_path_identity(
        paths.config_root / "products");
    const auto config_identity = existing_path_identity(
        kano_identity.final_path / "backlog_config.toml");
    if (!kano_identity.is_directory || !products_identity.is_directory ||
        config_identity.is_directory ||
        !is_direct_child(kano_identity.final_path, paths.config_root) ||
        !is_direct_child(products_identity.final_path, paths.config_root) ||
        !is_direct_child(
            config_identity.final_path, kano_identity.final_path)) {
        throw std::runtime_error("shared_backlog_config_not_found_or_unsafe");
    }
    paths.kano_root = kano_identity.final_path;
    paths.products_root = products_identity.final_path;
    paths.config_path = config_identity.final_path;

    paths.cache_root = paths.kano_root / "cache";
    if (std::filesystem::exists(paths.cache_root)) {
        const auto cache_identity = existing_path_identity(paths.cache_root);
        if (!cache_identity.is_directory ||
            !is_direct_child(cache_identity.final_path, paths.kano_root)) {
            throw std::runtime_error("product_registration_cache_unsafe");
        }
        paths.cache_root = cache_identity.final_path;
    }
    paths.registrations_root = paths.cache_root / "product-registrations";
    if (std::filesystem::exists(paths.registrations_root)) {
        const auto registrations_identity = existing_path_identity(
            paths.registrations_root);
        if (!registrations_identity.is_directory ||
            !is_direct_child(
                registrations_identity.final_path, paths.cache_root)) {
            throw std::runtime_error(
                "product_registration_cache_authority_mismatch");
        }
        paths.registrations_root = registrations_identity.final_path;
    }
    if (!is_lowercase_sha256(options.plan_hash)) {
        throw std::runtime_error("invalid_product_registration_plan_hash");
    }
    paths.transaction = paths.registrations_root / options.plan_hash;
    if (std::filesystem::exists(paths.transaction)) {
        const auto transaction_identity = existing_path_identity(
            paths.transaction);
        if (!transaction_identity.is_directory ||
            !is_direct_child(
                transaction_identity.final_path,
                paths.registrations_root)) {
            throw std::runtime_error(
                "product_registration_transaction_unsafe");
        }
        paths.transaction = transaction_identity.final_path;
    }
    paths.before = paths.transaction / "config.before";
    paths.after = paths.transaction / "config.after";
    paths.receipt = paths.transaction / "receipt.json";
    paths.journal = paths.transaction / "journal.json";
    return paths;
}

std::filesystem::path ensure_direct_child_directory(
    const std::filesystem::path& path,
    const std::filesystem::path& parent,
    std::string_view failure
) {
    if (!std::filesystem::exists(path)) {
        std::error_code create_error;
        if (!std::filesystem::create_directory(path, create_error) ||
            create_error) {
            throw std::runtime_error(std::string(failure));
        }
    }
    const auto identity = existing_path_identity(path);
    if (!identity.is_directory ||
        !is_direct_child(identity.final_path, parent)) {
        throw std::runtime_error(std::string(failure));
    }
    return identity.final_path;
}

void ensure_registration_cache_authority(EvidencePaths& paths) {
    paths.cache_root = ensure_direct_child_directory(
        paths.cache_root, paths.kano_root,
        "product_registration_cache_unsafe");
    paths.registrations_root = ensure_direct_child_directory(
        paths.cache_root / "product-registrations", paths.cache_root,
        "product_registration_cache_authority_mismatch");
    paths.transaction = paths.registrations_root /
                        paths.transaction.filename();
    paths.before = paths.transaction / "config.before";
    paths.after = paths.transaction / "config.after";
    paths.receipt = paths.transaction / "receipt.json";
    paths.journal = paths.transaction / "journal.json";
}

std::string stage_prefix(const std::string& plan_hash) {
    return ".kob-stage." + compact_plan_token(plan_hash) + ".";
}

bool staged_directory_is_reclaimable(
    const std::filesystem::path& stage,
    const std::filesystem::path& registrations_root,
    const std::string& plan_hash
) {
    try {
        const auto stage_identity = existing_path_identity(stage);
        if (!stage_identity.is_directory ||
            !is_direct_child(stage_identity.final_path, registrations_root)) {
            return false;
        }
        const auto name = stage.filename().generic_string();
        const auto prefix = stage_prefix(plan_hash);
        if (!name.starts_with(prefix) || name.size() == prefix.size()) {
            return false;
        }
        const auto token = name.substr(prefix.size());
        if (!is_valid_operation_token(token)) {
            return false;
        }
        std::set<std::string> names;
        std::optional<Json::Value> owner;
        std::error_code iterator_error;
        for (std::filesystem::directory_iterator iterator(
                 stage, std::filesystem::directory_options::none,
                 iterator_error), end;
             iterator != end && !iterator_error;
             iterator.increment(iterator_error)) {
            if (names.size() >= 5u || path_entry_is_reparse(iterator->path()) ||
                !iterator->is_regular_file()) {
                return false;
            }
            const auto entry_name =
                iterator->path().filename().generic_string();
            if (!names.insert(entry_name).second) {
                return false;
            }
            const auto owner_name = "stage-owner." + token + ".json";
            if (entry_name == owner_name) {
                owner = parse_json(read_file(iterator->path(), 64u * 1024u));
            } else if (entry_name != "config.before" &&
                       entry_name != "config.after" &&
                       entry_name != "receipt.json" &&
                       entry_name != "journal.json") {
                return false;
            }
        }
        if (iterator_error) {
            return false;
        }
        const auto owner_name = "stage-owner." + token + ".json";
        const std::set<std::string> complete_names{
            "config.before", "config.after", "receipt.json", "journal.json",
        };
        auto complete_names_with_owner = complete_names;
        complete_names_with_owner.insert(owner_name);
        if (owner) {
            if (names != complete_names_with_owner ||
                (*owner)["schema"].asString() !=
                    "kob.product_registration.stage.v1" ||
                (*owner)["plan_hash"].asString() != plan_hash ||
                (*owner)["owner_token"].asString() != token ||
                !is_valid_agent((*owner)["apply_agent"].asString())) {
                return false;
            }
        } else if (names != complete_names) {
            return false;
        }
        const auto before = read_file(stage / "config.before");
        const auto after = read_file(stage / "config.after");
        const auto receipt_bytes = read_file(stage / "receipt.json");
        const auto receipt = parse_json(receipt_bytes);
        const auto journal = parse_json(read_file(stage / "journal.json"));
        const auto apply_agent = receipt["apply_agent"].asString();
        return receipt["schema"].asString() ==
                   kano::backlog_ops::kProductRegistrationReceiptSchema &&
                journal["schema"].asString() ==
                    kano::backlog_ops::kProductRegistrationJournalSchema &&
                receipt["plan_hash"].asString() == plan_hash &&
                journal["plan_hash"].asString() == plan_hash &&
                is_valid_agent(apply_agent) &&
                journal["apply_agent"].asString() == apply_agent &&
                (!owner ||
                 (*owner)["apply_agent"].asString() == apply_agent) &&
                sha256_hex(before) ==
                    journal["config_before_sha256"].asString() &&
                sha256_hex(before) ==
                    receipt["config_before_sha256"].asString() &&
                sha256_hex(after) ==
                    journal["config_after_sha256"].asString() &&
                sha256_hex(after) ==
                    receipt["config_after_sha256"].asString() &&
                sha256_hex(receipt_bytes) ==
                    journal["receipt_sha256"].asString();
    } catch (const std::exception&) {
        return false;
    }
}

void reclaim_same_plan_staging(
    const EvidencePaths& paths,
    const std::string& plan_hash
) {
    const auto prefix = stage_prefix(plan_hash);
    std::size_t matches = 0;
    std::error_code iterator_error;
    for (std::filesystem::directory_iterator iterator(
             paths.registrations_root,
             std::filesystem::directory_options::none,
             iterator_error), end;
         iterator != end && !iterator_error;
         iterator.increment(iterator_error)) {
        const auto name = iterator->path().filename().generic_string();
        if (!name.starts_with(prefix)) {
            continue;
        }
        if (matches++ >= 128u) {
            break;
        }
        if (!staged_directory_is_reclaimable(
                iterator->path(), paths.registrations_root, plan_hash)) {
            continue;
        }
        std::error_code remove_error;
        std::filesystem::remove_all(iterator->path(), remove_error);
        if (remove_error) {
            break;
        }
    }
}

struct StagedEvidence {
    std::filesystem::path directory;
    std::filesystem::path owner;
    Json::Value journal;
};

StagedEvidence create_staged_evidence(
    const EvidencePaths& paths,
    const PreparedRegistration& prepared,
    const std::string& actor,
    const std::string& receipt_bytes,
    const Json::Value& journal
) {
    StagedEvidence staged;
    staged.journal = journal;
    for (std::size_t attempt = 0; attempt < 32u; ++attempt) {
        const auto token = unique_operation_token();
        staged.directory = paths.registrations_root /
            (stage_prefix(prepared.plan.plan_hash) + token);
        std::error_code create_error;
        if (!std::filesystem::create_directory(
                staged.directory, create_error) || create_error) {
            continue;
        }
        staged.owner = staged.directory /
            ("stage-owner." + token + ".json");
        Json::Value owner(Json::objectValue);
        owner["schema"] = "kob.product_registration.stage.v1";
        owner["plan_hash"] = prepared.plan.plan_hash;
        owner["owner_token"] = token;
        owner["apply_agent"] = actor;
        owner["pid"] = static_cast<Json::UInt64>(current_process_id_value());
        owner["created_at"] = current_utc_timestamp();
        if (!write_file_exclusive(staged.owner, json_string(owner, true))) {
            throw std::runtime_error(
                "product_registration_stage_owner_create_failed");
        }
        write_new_file_atomic(
            staged.directory / "config.before", prepared.config_before);
        write_new_file_atomic(
            staged.directory / "config.after", prepared.config_after);
        write_new_file_atomic(
            staged.directory / "receipt.json", receipt_bytes);
        write_new_file_atomic(
            staged.directory / "journal.json", json_string(journal, true));
        return staged;
    }
    throw std::runtime_error(
        "product_registration_stage_exclusive_create_failed");
}

void publish_staged_evidence(
    const EvidencePaths& paths,
    const StagedEvidence& staged
) {
    std::error_code remove_error;
    if (!std::filesystem::remove(staged.owner, remove_error) || remove_error) {
        throw std::runtime_error(
            "product_registration_stage_owner_remove_failed");
    }
    std::error_code rename_error;
    std::filesystem::rename(
        staged.directory, paths.transaction, rename_error);
    if (rename_error) {
        throw std::runtime_error(
            "product_registration_evidence_publish_failed");
    }
}

void write_journal(
    const EvidencePaths& paths,
    const Json::Value& journal
) {
    replace_file_atomic(
        paths.journal,
        json_string(journal, true),
        journal["plan_hash"].asString(),
        "journal",
        paths.registrations_root);
}

class ConfigMutationGuard {
public:
    explicit ConfigMutationGuard(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_ = CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING ||
                error == ERROR_SHARING_VIOLATION) {
                throw std::runtime_error("product_registration_lock_active");
            }
            throw std::runtime_error(
                "product_registration_lock_guard_open_failed");
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileType(handle_) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(handle_, &information) ||
            (information.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error(
                "product_registration_lock_guard_unsafe");
        }
        if (!LockFileEx(
                handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                0, MAXDWORD, MAXDWORD, &overlapped_)) {
            const auto error = GetLastError();
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) {
                throw std::runtime_error("product_registration_lock_active");
            }
            throw std::runtime_error(
                "product_registration_lock_guard_acquire_failed");
        }
        locked_ = true;
#else
        descriptor_ = ::open(
            path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor_ < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                throw std::runtime_error("product_registration_lock_active");
            }
            throw std::runtime_error(
                errno == ELOOP
                    ? "product_registration_lock_guard_unsafe"
                    : "product_registration_lock_guard_open_failed");
        }
        struct stat information {};
        if (::fstat(descriptor_, &information) != 0 ||
            !S_ISREG(information.st_mode)) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw std::runtime_error(
                "product_registration_lock_guard_unsafe");
        }
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            const auto error = errno;
            ::close(descriptor_);
            descriptor_ = -1;
            if (error == EWOULDBLOCK || error == EAGAIN) {
                throw std::runtime_error("product_registration_lock_active");
            }
            throw std::runtime_error(
                "product_registration_lock_guard_acquire_failed");
        }
        locked_ = true;
#endif
    }

    ~ConfigMutationGuard() { release(); }

    ConfigMutationGuard(const ConfigMutationGuard&) = delete;
    ConfigMutationGuard& operator=(const ConfigMutationGuard&) = delete;

    ConfigMutationGuard(ConfigMutationGuard&& other) noexcept {
        move_from(other);
    }

    ConfigMutationGuard& operator=(ConfigMutationGuard&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    void release() noexcept {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        if (locked_) {
            (void)UnlockFileEx(
                handle_, 0, MAXDWORD, MAXDWORD, &overlapped_);
        }
        (void)CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        overlapped_ = {};
#else
        if (descriptor_ < 0) {
            return;
        }
        if (locked_) {
            (void)::flock(descriptor_, LOCK_UN);
        }
        (void)::close(descriptor_);
        descriptor_ = -1;
#endif
        locked_ = false;
    }

private:
    void move_from(ConfigMutationGuard& other) noexcept {
#ifdef _WIN32
        handle_ = other.handle_;
        overlapped_ = other.overlapped_;
        other.handle_ = INVALID_HANDLE_VALUE;
        other.overlapped_ = {};
#else
        descriptor_ = other.descriptor_;
        other.descriptor_ = -1;
#endif
        locked_ = other.locked_;
        other.locked_ = false;
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
#else
    int descriptor_ = -1;
#endif
    bool locked_ = false;
};

class DirectoryLock {
public:
    DirectoryLock(
        std::filesystem::path path,
        std::string plan_hash,
        std::function<void(std::string_view)> test_checkpoint = {}
    ) : path_(validated_lock_path(path, plan_hash)),
        plan_hash_(std::move(plan_hash)),
        test_checkpoint_(std::move(test_checkpoint)),
        guard_(path_.parent_path() /
               "product-registration.config.guard.lock") {
        reclaim_stale_artifacts();
        for (std::size_t attempt = 0; attempt < 3u; ++attempt) {
            owner_token_ = unique_operation_token();
            auto candidate = path_;
            candidate += ".candidate.";
            candidate += owner_token_;
            std::error_code error;
            if (!std::filesystem::create_directory(candidate, error) || error) {
                continue;
            }
            owner_name_ = "owner." + owner_token_ + ".json";
            std::optional<LockOwner> expected_owner;
            try {
                Json::Value owner(Json::objectValue);
                owner["schema"] = "kob.product_registration.lock.v1";
                owner["plan_hash"] = plan_hash_;
                owner["owner_token"] = owner_token_;
                owner["pid"] = static_cast<Json::UInt64>(
                    current_process_id_value());
                owner["created_at"] = current_utc_timestamp();
                if (!write_file_exclusive(
                        candidate / owner_name_, json_string(owner, true))) {
                    throw std::runtime_error(
                        "product_registration_lock_owner_write_failed");
                }
                expected_owner = validated_owner(candidate, owner_token_);
                if (!expected_owner ||
                    expected_owner->plan_hash != plan_hash_ ||
                    expected_owner->pid != current_process_id_value()) {
                    throw std::runtime_error(
                        "product_registration_lock_owner_validation_failed");
                }
                std::filesystem::rename(candidate, path_, error);
                if (!error) {
                    const auto published_owner =
                        validated_owner(path_, owner_token_);
                    if (!published_owner ||
                        !same_owner(*expected_owner, *published_owner)) {
                        throw std::runtime_error(
                            "product_registration_lock_active");
                    }
                    owns_ = true;
                    return;
                }
            } catch (...) {
                (void)release_owned_candidate(candidate);
                throw;
            }
            if (!release_owned_candidate(candidate)) {
                throw std::runtime_error("product_registration_lock_active");
            }
            if (!reclaim_stale_lock()) {
                throw std::runtime_error("product_registration_lock_active");
            }
        }
        throw std::runtime_error("product_registration_lock_active");
    }

    ~DirectoryLock() {
        release_owned_lock();
        guard_.release();
    }

    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;

private:
    struct LockOwner {
        std::string plan_hash;
        std::string token;
        std::string created_at;
        std::uint64_t pid = 0;
    };

    static std::filesystem::path validated_lock_path(
        const std::filesystem::path& path,
        const std::string& plan_hash
    ) {
        if (!is_lowercase_sha256(plan_hash) ||
            !std::filesystem::is_directory(path.parent_path()) ||
            existing_path_has_reparse_component(path.parent_path())) {
            throw std::runtime_error(
                "product_registration_lock_parent_missing");
        }
        const auto parent_identity = existing_path_identity(
            path.parent_path());
        if (!parent_identity.is_directory ||
            !is_direct_child(path, parent_identity.final_path)) {
            throw std::runtime_error(
                "product_registration_lock_parent_unsafe");
        }
        return parent_identity.final_path / path.filename();
    }

    static bool process_is_alive(std::uint64_t pid) {
        if (pid == 0) {
            return false;
        }
#ifdef _WIN32
        const auto process = OpenProcess(
            SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (!process) {
            return GetLastError() == ERROR_ACCESS_DENIED;
        }
        const auto wait = WaitForSingleObject(process, 0);
        CloseHandle(process);
        return wait == WAIT_TIMEOUT;
#else
        if (kill(static_cast<pid_t>(pid), 0) == 0) {
            return true;
        }
        return errno == EPERM;
#endif
    }

    static bool same_owner(const LockOwner& left, const LockOwner& right) {
        return left.plan_hash == right.plan_hash &&
               left.token == right.token &&
               left.created_at == right.created_at && left.pid == right.pid;
    }

    std::optional<LockOwner> validated_owner(
        const std::filesystem::path& directory,
        std::optional<std::string_view> required_token = std::nullopt
    ) const {
        if (!is_direct_child(directory, path_.parent_path()) ||
            !std::filesystem::is_directory(directory) ||
            path_entry_is_reparse(directory)) {
            return std::nullopt;
        }
        ExistingPathIdentity directory_identity;
        try {
            directory_identity = existing_path_identity(directory);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (!directory_identity.is_directory ||
            !path_equal(directory_identity.final_path, directory) ||
            !is_direct_child(
                directory_identity.final_path, path_.parent_path())) {
            return std::nullopt;
        }
        std::optional<std::filesystem::path> owner_path;
        std::size_t entries = 0;
        std::error_code iterator_error;
        for (std::filesystem::directory_iterator iterator(
                 directory, std::filesystem::directory_options::none,
                 iterator_error), end;
             iterator != end && !iterator_error;
             iterator.increment(iterator_error)) {
            if (++entries > 1u || path_entry_is_reparse(iterator->path()) ||
                !iterator->is_regular_file()) {
                return std::nullopt;
            }
            owner_path = iterator->path();
        }
        if (iterator_error || entries != 1u || !owner_path) {
            return std::nullopt;
        }
        try {
            const auto owner_identity = existing_path_identity(*owner_path);
            if (owner_identity.is_directory ||
                !path_equal(owner_identity.final_path, *owner_path) ||
                !is_direct_child(
                    owner_identity.final_path,
                    directory_identity.final_path)) {
                return std::nullopt;
            }
            const auto owner = parse_json(read_file(*owner_path, 64u * 1024u));
            const auto token = owner["owner_token"].asString();
            if (owner["schema"].asString() !=
                    "kob.product_registration.lock.v1" ||
                !is_lowercase_sha256(owner["plan_hash"].asString()) ||
                token.empty() ||
                owner_path->filename() !=
                    std::filesystem::path("owner." + token + ".json") ||
                (required_token && token != *required_token) ||
                !owner["pid"].isUInt64()) {
                return std::nullopt;
            }
            return LockOwner{
                .plan_hash = owner["plan_hash"].asString(),
                .token = token,
                .created_at = owner["created_at"].asString(),
                .pid = owner["pid"].asUInt64(),
            };
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::filesystem::path quarantine_path(
        std::string_view kind,
        std::string_view owner_token
    ) const {
        auto quarantine = path_;
        quarantine += ".";
        quarantine += kind;
        quarantine += ".";
        quarantine += owner_token;
        quarantine += ".";
        quarantine += unique_operation_token();
        return quarantine;
    }

    static void restore_if_absent(
        const std::filesystem::path& quarantine,
        const std::filesystem::path& source
    ) noexcept {
        try {
            std::error_code status_error;
            const auto status = std::filesystem::symlink_status(
                source, status_error);
            if (status_error ||
                status.type() != std::filesystem::file_type::not_found) {
                return;
            }
            std::error_code rename_error;
            std::filesystem::rename(quarantine, source, rename_error);
        } catch (const std::exception&) {
        }
    }

    bool quarantine_and_delete(
        const std::filesystem::path& source,
        const LockOwner& expected_owner,
        std::string_view kind,
        bool require_dead_process
    ) const {
        const auto quarantine = quarantine_path(kind, expected_owner.token);
        std::error_code rename_error;
        std::filesystem::rename(source, quarantine, rename_error);
        if (rename_error) {
            return false;
        }
        const auto quarantined_owner = validated_owner(
            quarantine, expected_owner.token);
        if (!quarantined_owner ||
            !same_owner(expected_owner, *quarantined_owner) ||
            (require_dead_process &&
             process_is_alive(quarantined_owner->pid))) {
            restore_if_absent(quarantine, source);
            return false;
        }
        std::error_code remove_error;
        const auto removed = std::filesystem::remove_all(
            quarantine, remove_error);
        return !remove_error && removed > 0u;
    }

    bool reclaim_stale_lock() const {
        const auto owner = validated_owner(path_);
        if (!owner || process_is_alive(owner->pid)) {
            return false;
        }
        if (test_checkpoint_) {
            test_checkpoint_("after_stale_owner_validation");
        }
        return quarantine_and_delete(
            path_, *owner, "stale-quarantine", true);
    }

    std::optional<std::string> encoded_owner_token(
        std::string_view name
    ) const {
        const auto base = path_.filename().generic_string();
        const auto candidate_prefix = base + ".candidate.";
        if (name.starts_with(candidate_prefix)) {
            const auto token = name.substr(candidate_prefix.size());
            if (is_valid_operation_token(token)) {
                return std::string(token);
            }
            return std::nullopt;
        }
        constexpr std::array<std::string_view, 5> quarantine_kinds = {
            "stale-quarantine", "release-quarantine",
            "candidate-quarantine", "cleanup-quarantine",
            "orphan-quarantine",
        };
        for (const auto kind : quarantine_kinds) {
            const auto prefix = base + "." + std::string(kind) + ".";
            if (!name.starts_with(prefix)) {
                continue;
            }
            const auto encoded = name.substr(prefix.size());
            const auto separator = encoded.find('.');
            if (separator == std::string_view::npos ||
                !is_valid_operation_token(encoded.substr(0, separator)) ||
                !is_valid_operation_token(encoded.substr(separator + 1u))) {
                return std::nullopt;
            }
            return std::string(encoded.substr(0, separator));
        }
        return std::nullopt;
    }

    bool matching_orphan_name(std::string_view name) const {
        const auto base = path_.filename().generic_string();
        return name.starts_with(base + ".candidate.") ||
               name.starts_with(base + ".stale-quarantine.") ||
               name.starts_with(base + ".release-quarantine.") ||
               name.starts_with(base + ".candidate-quarantine.") ||
               name.starts_with(base + ".cleanup-quarantine.") ||
               name.starts_with(base + ".orphan-quarantine.");
    }

    void reclaim_stale_artifacts() const {
        std::size_t matches = 0;
        std::error_code iterator_error;
        for (std::filesystem::directory_iterator iterator(
                 path_.parent_path(),
                 std::filesystem::directory_options::none,
                 iterator_error), end;
             iterator != end && !iterator_error;
             iterator.increment(iterator_error)) {
            const auto name = iterator->path().filename().generic_string();
            if (!matching_orphan_name(name)) {
                continue;
            }
            if (matches++ >= 128u) {
                break;
            }
            const auto token = encoded_owner_token(name);
            if (!token) {
                continue;
            }
            const auto owner = validated_owner(iterator->path(), *token);
            if (!owner || process_is_alive(owner->pid)) {
                continue;
            }
            if (!quarantine_and_delete(
                    iterator->path(), *owner,
                    "orphan-quarantine", true)) {
                throw std::runtime_error("product_registration_lock_active");
            }
        }
    }

    bool release_owned_candidate(
        const std::filesystem::path& candidate
    ) const noexcept {
        try {
            const auto owner = validated_owner(candidate, owner_token_);
            if (!owner || owner->plan_hash != plan_hash_ ||
                owner->pid != current_process_id_value()) {
                return false;
            }
            return quarantine_and_delete(
                candidate, *owner, "candidate-quarantine", false);
        } catch (const std::exception&) {
            return false;
        }
    }

    void release_owned_lock() noexcept {
        if (!owns_) {
            return;
        }
        try {
            const auto owner = validated_owner(path_, owner_token_);
            if (owner && owner->plan_hash == plan_hash_ &&
                owner->pid == current_process_id_value()) {
                (void)quarantine_and_delete(
                    path_, *owner, "release-quarantine", false);
            }
        } catch (const std::exception&) {
        }
        owns_ = false;
    }

    std::filesystem::path path_;
    std::string plan_hash_;
    std::function<void(std::string_view)> test_checkpoint_;
    ConfigMutationGuard guard_;
    std::string owner_token_;
    std::string owner_name_;
    bool owns_ = false;
};

struct ValidatedEvidence {
    EvidencePaths paths;
    Json::Value journal;
    Json::Value receipt;
    Json::Value plan;
    std::string before;
    std::string after;
    std::string product;
    std::string product_name;
    std::string prefix;
    std::vector<std::string> aliases;
    std::vector<std::string> repo_bindings;
    std::string apply_agent;
    std::optional<std::string> recovery_agent;
    std::filesystem::path external_root;
    std::filesystem::path canonical_destination;
};

ValidatedEvidence load_validated_evidence(
    const kano::backlog_ops::ProductRegistrationOps::RecoveryOptions& options
) {
    ValidatedEvidence evidence;
    evidence.paths = evidence_paths(options);
    if (!std::filesystem::is_regular_file(evidence.paths.journal)) {
        throw std::runtime_error("product_registration_journal_not_found");
    }
    if (!std::filesystem::is_regular_file(evidence.paths.before) ||
        !std::filesystem::is_regular_file(evidence.paths.after) ||
        !std::filesystem::is_regular_file(evidence.paths.receipt)) {
        throw std::runtime_error("product_registration_evidence_incomplete");
    }
    for (const auto& file : {
             evidence.paths.before,
             evidence.paths.after,
             evidence.paths.receipt,
             evidence.paths.journal,
         }) {
        const auto identity = existing_path_identity(file);
        if (identity.is_directory ||
            !is_direct_child(
                identity.final_path, evidence.paths.transaction)) {
            throw std::runtime_error("product_registration_evidence_unsafe");
        }
    }
    evidence.before = read_file(evidence.paths.before, 64u * 1024u * 1024u);
    evidence.after = read_file(evidence.paths.after, 64u * 1024u * 1024u);
    evidence.receipt = parse_json(
        read_file(evidence.paths.receipt, 4u * 1024u * 1024u));
    evidence.journal = parse_json(
        read_file(evidence.paths.journal, 4u * 1024u * 1024u));
    if (evidence.receipt["schema"].asString() !=
            kano::backlog_ops::kProductRegistrationReceiptSchema ||
        evidence.journal["schema"].asString() !=
            kano::backlog_ops::kProductRegistrationJournalSchema) {
        throw std::runtime_error(
            "unsupported_product_registration_evidence_schema");
    }
    if (evidence.receipt["plan_hash"].asString() != options.plan_hash ||
        evidence.journal["plan_hash"].asString() != options.plan_hash) {
        throw std::runtime_error("journal_plan_hash_mismatch");
    }
    if (!evidence.receipt["plan"].isObject()) {
        throw std::runtime_error("receipt_plan_missing");
    }
    evidence.plan = evidence.receipt["plan"];
    if (evidence.plan["plan_hash"].asString() != options.plan_hash) {
        throw std::runtime_error("embedded_plan_hash_mismatch");
    }
    auto plan_for_hash = evidence.plan;
    plan_for_hash["plan_hash"] = "";
    if (sha256_hex(json_string(plan_for_hash, false)) != options.plan_hash) {
        throw std::runtime_error("embedded_plan_hash_mismatch");
    }
    const auto plan_aliases = persisted_selector_array(
        evidence.plan, "aliases");
    const auto plan_repo_bindings = persisted_selector_array(
        evidence.plan, "repo_bindings");
    const auto request_aliases = persisted_selector_array(
        evidence.plan["request"], "aliases");
    const auto request_repo_bindings = persisted_selector_array(
        evidence.plan["request"], "repo_bindings");
    const auto receipt_aliases = persisted_selector_array(
        evidence.receipt, "aliases");
    const auto receipt_repo_bindings = persisted_selector_array(
        evidence.receipt, "repo_bindings");
    const auto journal_aliases = persisted_selector_array(
        evidence.journal, "aliases");
    const auto journal_repo_bindings = persisted_selector_array(
        evidence.journal, "repo_bindings");
    if (!plan_aliases || !plan_repo_bindings || !request_aliases ||
        !request_repo_bindings || !receipt_aliases ||
        !receipt_repo_bindings || !journal_aliases ||
        !journal_repo_bindings || *plan_aliases != *request_aliases ||
        *plan_aliases != *receipt_aliases ||
        *plan_aliases != *journal_aliases ||
        *plan_repo_bindings != *request_repo_bindings ||
        *plan_repo_bindings != *receipt_repo_bindings ||
        *plan_repo_bindings != *journal_repo_bindings) {
        throw std::runtime_error("invalid_product_registration_selectors");
    }
    evidence.aliases = *plan_aliases;
    evidence.repo_bindings = *plan_repo_bindings;
    const auto before_sha256 = sha256_hex(evidence.before);
    const auto after_sha256 = sha256_hex(evidence.after);
    if (before_sha256 != evidence.plan["config_revision"].asString() ||
        after_sha256 !=
            evidence.plan["proposed_config_revision"].asString() ||
        before_sha256 !=
            evidence.receipt["config_before_sha256"].asString() ||
        after_sha256 !=
            evidence.receipt["config_after_sha256"].asString() ||
        before_sha256 !=
            evidence.journal["config_before_sha256"].asString() ||
        after_sha256 !=
            evidence.journal["config_after_sha256"].asString() ||
        sha256_hex(read_file(evidence.paths.receipt)) !=
            evidence.journal["receipt_sha256"].asString()) {
        throw std::runtime_error("immutable_evidence_hash_mismatch");
    }
    evidence.product = evidence.plan["product"].asString();
    evidence.product_name = evidence.plan["product_name"].asString();
    evidence.prefix = evidence.plan["prefix"].asString();
    evidence.apply_agent = evidence.receipt["apply_agent"].asString();
    if (evidence.journal["recovery_agent"].isString()) {
        evidence.recovery_agent =
            evidence.journal["recovery_agent"].asString();
    }
    if (!is_valid_product_slug(evidence.product) ||
        !is_valid_product_name(evidence.product_name) ||
        !is_valid_prefix(evidence.prefix) ||
        !is_valid_agent(evidence.apply_agent) ||
        evidence.journal["apply_agent"].asString() !=
            evidence.apply_agent ||
        (evidence.recovery_agent &&
         !is_valid_agent(*evidence.recovery_agent)) ||
        !evidence.journal["attempts"].isArray() ||
        evidence.journal["attempts"].size() == 0u ||
        evidence.journal["attempts"].size() > 1024u) {
        throw std::runtime_error("invalid_product_registration_evidence");
    }
    const auto entry = find_registry_entry(evidence.after, evidence.product);
    if (!entry || entry->name != evidence.product_name ||
        entry->prefix != evidence.prefix ||
        entry->aliases != evidence.aliases ||
        entry->repo_bindings != evidence.repo_bindings) {
        throw std::runtime_error("config_after_registration_mismatch");
    }
    const std::filesystem::path configured_root(entry->backlog_root);
    evidence.external_root = normalized_absolute(
        configured_root.is_absolute()
            ? configured_root
            : evidence.paths.config_root / configured_root);
    if (std::filesystem::exists(evidence.external_root)) {
        evidence.external_root =
            existing_path_identity(evidence.external_root).final_path;
    }
    evidence.canonical_destination =
        (evidence.paths.products_root / evidence.product).lexically_normal();
    if (sha256_hex(evidence.paths.config_path.generic_string()) !=
            evidence.plan["config_path_digest"].asString() ||
        sha256_hex(evidence.external_root.generic_string()) !=
            evidence.plan["external_root_path_digest"].asString() ||
        sha256_hex(evidence.canonical_destination.generic_string()) !=
            evidence.plan["canonical_destination_path_digest"].asString() ||
        !path_equal(
            normalized_absolute(
                configured_root.is_absolute()
                    ? configured_root
                    : evidence.paths.config_root / configured_root),
            evidence.external_root)) {
        throw std::runtime_error("evidence_path_binding_mismatch");
    }
    return evidence;
}

bool options_match_evidence(
    const kano::backlog_ops::ProductRegistrationOps::PlanOptions& options,
    const ValidatedEvidence& evidence
) {
    if (!options.backlog_root.is_absolute() ||
        !options.request.external_root.is_absolute()) {
        return false;
    }
    try {
        const auto& limits = evidence.plan["limits"];
        bool has_empty_alias = false;
        bool has_empty_repo_binding = false;
        const auto aliases = normalize_selector_list(
            options.request.aliases, has_empty_alias);
        const auto repo_bindings = normalize_selector_list(
            options.request.repo_bindings, has_empty_repo_binding);
        auto requested_external = normalized_absolute(
            options.request.external_root);
        if (std::filesystem::exists(requested_external)) {
            requested_external =
                existing_path_identity(options.request.external_root).final_path;
        }
        return options.request.product == evidence.product &&
               options.request.product_name == evidence.product_name &&
               options.request.prefix == evidence.prefix &&
               !has_empty_alias && !has_empty_repo_binding &&
               aliases == evidence.aliases &&
               repo_bindings == evidence.repo_bindings &&
               path_equal(
                   existing_path_identity(options.backlog_root).final_path,
                   evidence.paths.config_root) &&
               path_equal(
                   requested_external,
                   evidence.external_root) &&
               limits["max_files"].asUInt64() == options.limits.max_files &&
               limits["max_bytes"].asUInt64() == options.limits.max_bytes &&
               limits["max_items"].asUInt64() == options.limits.max_items;
    } catch (const std::exception&) {
        return false;
    }
}

std::string current_source_revision(
    const ValidatedEvidence& evidence,
    std::vector<std::string>& failures
) {
    const auto& limits_json = evidence.plan["limits"];
    if (!limits_json.isObject() ||
        !limits_json["max_files"].isUInt64() ||
        !limits_json["max_bytes"].isUInt64() ||
        !limits_json["max_items"].isUInt64()) {
        failures.push_back("invalid_embedded_source_limits");
        return {};
    }
    const auto raw_max_files = limits_json["max_files"].asUInt64();
    const auto raw_max_bytes = limits_json["max_bytes"].asUInt64();
    const auto raw_max_items = limits_json["max_items"].asUInt64();
    if (raw_max_files > std::numeric_limits<std::size_t>::max() ||
        raw_max_bytes > std::numeric_limits<std::uintmax_t>::max() ||
        raw_max_items > std::numeric_limits<std::size_t>::max()) {
        failures.push_back("invalid_embedded_source_limits");
        return {};
    }
    const ProductRegistrationLimits limits{
        .max_files = static_cast<std::size_t>(raw_max_files),
        .max_bytes = static_cast<std::uintmax_t>(raw_max_bytes),
        .max_items = static_cast<std::size_t>(raw_max_items),
    };
    if (!registration_limit_blockers(limits).empty()) {
        failures.push_back("invalid_embedded_source_limits");
        return {};
    }
    if (!std::filesystem::is_directory(evidence.external_root) ||
        existing_path_has_reparse_component(evidence.external_root)) {
        failures.push_back("source_root_missing_or_unsafe");
        return {};
    }
    try {
        const auto identity = existing_path_identity(evidence.external_root);
        if (!identity.is_directory ||
            !path_equal(identity.final_path, evidence.external_root)) {
            failures.push_back("source_root_identity_mismatch");
            return {};
        }
    } catch (const std::exception&) {
        failures.push_back("source_root_identity_mismatch");
        return {};
    }
    std::vector<ManifestEntry> files;
    std::uintmax_t total_bytes = 0;
    std::size_t item_count = 0;
    PathEntryBudget entry_budget(limits.max_files);
    if (!std::filesystem::is_directory(evidence.external_root / "items")) {
        failures.push_back("source_items_missing");
    }
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        evidence.external_root,
        std::filesystem::directory_options::none,
        iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    if (iterator_error) {
        failures.push_back("source_inventory_open_failed");
        return {};
    }
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            failures.push_back("source_inventory_read_failed");
            break;
        }
        if (!entry_budget.charge()) {
            failures.push_back("source_path_entry_limit_exceeded");
            break;
        }
        if (path_entry_is_reparse(iterator->path())) {
            failures.push_back("source_symlink_or_reparse_not_supported");
            std::error_code directory_error;
            if (iterator->is_directory(directory_error) && !directory_error) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        const auto relative_path = iterator->path().lexically_relative(
            evidence.external_root);
        if (relative_path.empty() || relative_path.is_absolute() ||
            first_component(relative_path) == "..") {
            failures.push_back("source_path_escape_detected");
            continue;
        }
        std::error_code directory_error;
        if (iterator->is_directory(directory_error) && !directory_error) {
            if (is_derived_source_path(relative_path)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        std::error_code regular_error;
        if (!iterator->is_regular_file(regular_error) || regular_error) {
            continue;
        }
        try {
            if (is_derived_source_path(relative_path)) {
                continue;
            }
            if (files.size() >= limits.max_files) {
                failures.push_back("source_file_limit_exceeded");
                break;
            }
            const auto kind = classify_source_file(relative_path);
            if (kind == "canonical_item" && ++item_count > limits.max_items) {
                failures.push_back("source_item_limit_exceeded");
                break;
            }
            std::error_code size_error;
            const auto file_size = iterator->file_size(size_error);
            if (size_error) {
                failures.push_back("source_file_unreadable");
                continue;
            }
            if (file_size > limits.max_bytes - total_bytes) {
                failures.push_back("source_byte_limit_exceeded");
                break;
            }
            const auto digest = digest_file(iterator->path());
            if (digest.size != file_size) {
                failures.push_back("source_file_changed_during_inventory");
                break;
            }
            total_bytes += digest.size;
            files.push_back({
                relative_path.generic_string(), kind,
                digest.size, digest.sha256,
            });
        } catch (const std::exception&) {
            failures.push_back("source_file_unreadable");
        }
    }
    if (item_count == 0) {
        failures.push_back("source_items_empty");
    }
    std::sort(
        files.begin(), files.end(), [](const auto& left, const auto& right) {
            return left.relative < right.relative;
        });
    StreamingSha256 revision;
    for (const auto& file : files) {
        revision.update(file.relative);
        revision.update("\0", 1u);
        revision.update(std::to_string(file.size));
        revision.update("\0", 1u);
        revision.update(file.sha256);
        revision.update("\n", 1u);
    }
    return revision.final_hex();
}

std::vector<std::string> source_and_destination_failures(
    const ValidatedEvidence& evidence
) {
    std::vector<std::string> failures;
    try {
        const auto products_identity = existing_path_identity(
            evidence.paths.products_root);
        if (!products_identity.is_directory ||
            !path_equal(
                products_identity.final_path,
                evidence.paths.products_root) ||
            !is_direct_child(
                evidence.canonical_destination,
                products_identity.final_path)) {
            failures.push_back("canonical_destination_authority_mismatch");
        }
    } catch (const std::exception&) {
        failures.push_back("canonical_destination_authority_mismatch");
    }
    const auto revision = current_source_revision(evidence, failures);
    if (revision != evidence.plan["source_revision"].asString()) {
        failures.push_back("source_revision_mismatch");
    }
    if (std::filesystem::exists(evidence.canonical_destination)) {
        failures.push_back("canonical_destination_present");
    }
    sort_unique(failures);
    return failures;
}


std::vector<std::string> recovery_registry_failures(
    const ValidatedEvidence& evidence,
    const kano::backlog_ops::ProductRegistrationOps::PlanOptions& options
) {
    PreparedRegistration prepared;
    prepared.plan.request = options.request;
    prepared.plan.limits = options.limits;
    prepared.plan.product = evidence.product;
    prepared.plan.product_name = evidence.product_name;
    prepared.plan.prefix = evidence.prefix;
    prepared.plan.aliases = evidence.aliases;
    prepared.plan.repo_bindings = evidence.repo_bindings;
    prepared.plan.request.aliases = evidence.aliases;
    prepared.plan.request.repo_bindings = evidence.repo_bindings;
    prepared.config_root = evidence.paths.config_root;
    prepared.config_path = evidence.paths.config_path;
    prepared.products_root = evidence.paths.products_root;
    prepared.external_root = evidence.external_root;
    prepared.canonical_destination = evidence.canonical_destination;
    prepared.config_before = evidence.before;

    inventory_source(prepared);
    collect_source_identities(prepared);
    const auto registry = parse_registry(evidence.before);
    if (!registry) {
        add_blocker(prepared.plan, "shared_config_malformed");
    } else {
        scan_registry(prepared, *registry);
    }

    auto failures = prepared.plan.blockers;
    if (prepared.plan.registry_revision !=
        evidence.plan["registry_revision"].asString()) {
        failures.push_back("stale_registry_identity");
    }
    sort_unique(failures);
    return failures;
}

std::vector<std::string> registration_postcondition_failures(
    const ValidatedEvidence& evidence
) {
    auto failures = source_and_destination_failures(evidence);
    const auto current = read_file(evidence.paths.config_path);
    if (current != evidence.after) {
        failures.push_back("config_registration_not_exact");
    } else {
        ProductRegistrationRequest request{
            .product = evidence.product,
            .product_name = evidence.product_name,
            .prefix = evidence.prefix,
            .aliases = evidence.aliases,
            .repo_bindings = evidence.repo_bindings,
            .external_root = evidence.external_root,
        };
        if (!config_registration_matches(
                current,
                evidence.paths.config_root,
                request,
                evidence.external_root)) {
            failures.push_back("external_root_resolution_mismatch");
        }
    }
    sort_unique(failures);
    return failures;
}

Json::Value make_receipt(
    const PreparedRegistration& prepared,
    const std::string& actor
) {
    Json::Value receipt(Json::objectValue);
    receipt["schema"] = kano::backlog_ops::kProductRegistrationReceiptSchema;
    receipt["plan_hash"] = prepared.plan.plan_hash;
    receipt["apply_agent"] = actor;
    receipt["created_at"] = current_utc_timestamp();
    receipt["config_before_sha256"] = sha256_hex(prepared.config_before);
    receipt["config_after_sha256"] = sha256_hex(prepared.config_after);
    receipt["config_ref"] = prepared.plan.config_ref;
    receipt["source_root_ref"] = prepared.plan.source_root_ref;
    receipt["canonical_destination_ref"] =
        prepared.plan.canonical_destination_ref;
    receipt["aliases"] = string_array(prepared.plan.aliases);
    receipt["repo_bindings"] = string_array(prepared.plan.repo_bindings);
    receipt["plan"] = plan_json(prepared.plan, true);
    return receipt;
}

Json::Value make_journal(
    const PreparedRegistration& prepared,
    const std::string& actor,
    const std::string& receipt_bytes
) {
    Json::Value journal(Json::objectValue);
    journal["schema"] = kano::backlog_ops::kProductRegistrationJournalSchema;
    journal["status"] = "prepared";
    journal["stage"] = "prepared";
    journal["plan_hash"] = prepared.plan.plan_hash;
    journal["apply_agent"] = actor;
    journal["recovery_agent"] = Json::nullValue;
    journal["created_at"] = current_utc_timestamp();
    journal["config_before_sha256"] = sha256_hex(prepared.config_before);
    journal["config_after_sha256"] = sha256_hex(prepared.config_after);
    journal["receipt_sha256"] = sha256_hex(receipt_bytes);
    journal["aliases"] = string_array(prepared.plan.aliases);
    journal["repo_bindings"] = string_array(prepared.plan.repo_bindings);
    Json::Value attempt(Json::objectValue);
    attempt["sequence"] = 1u;
    attempt["kind"] = "apply";
    attempt["agent"] = actor;
    attempt["started_at"] = current_utc_timestamp();
    attempt["outcome"] = "prepared";
    journal["attempts"] = Json::Value(Json::arrayValue);
    journal["attempts"].append(std::move(attempt));
    return journal;
}

void append_recovery_attempt(
    Json::Value& journal,
    const std::string& actor
) {
    if (!journal["attempts"].isArray() ||
        journal["attempts"].size() >= 1024u) {
        throw std::runtime_error("product_registration_attempt_limit_exceeded");
    }
    Json::Value attempt(Json::objectValue);
    attempt["sequence"] =
        static_cast<Json::UInt64>(journal["attempts"].size() + 1u);
    attempt["kind"] = "recovery";
    attempt["agent"] = actor;
    attempt["started_at"] = current_utc_timestamp();
    attempt["outcome"] = "started";
    journal["attempts"].append(std::move(attempt));
    journal["recovery_agent"] = actor;
}

void finish_latest_attempt(
    Json::Value& journal,
    const std::string& outcome
) {
    if (!journal["attempts"].isArray() ||
        journal["attempts"].size() == 0u) {
        throw std::runtime_error("product_registration_attempt_history_missing");
    }
    auto& attempt = journal["attempts"][journal["attempts"].size() - 1u];
    attempt["outcome"] = outcome;
    attempt["finished_at"] = current_utc_timestamp();
}

void set_recovery_result_actor(
    kano::backlog_ops::ProductRegistrationResult& result,
    const ValidatedEvidence& evidence
) {
    result.apply_agent = evidence.apply_agent;
    result.recovery_agent = evidence.recovery_agent;
}

std::vector<std::string> stale_receipts(
    const ProductRegistrationPlan& fresh,
    const std::optional<CachedPlan>& reviewed
) {
    std::vector<std::string> receipts;
    if (!reviewed) {
        receipts.push_back("expected_plan_hash_mismatch");
        return receipts;
    }
    if (reviewed->plan.config_revision != fresh.config_revision) {
        const bool registry_claimed = std::any_of(
            fresh.blockers.begin(), fresh.blockers.end(), [](const auto& value) {
                return value.starts_with("product_already_registered") ||
                       value.starts_with("product_case_fold_collision") ||
                       value.starts_with("prefix_collision") ||
                       value.starts_with("product_selector_collision") ||
                       value.starts_with("external_root_collision");
            });
        receipts.push_back(
            registry_claimed ? "stale_registry_identity" :
                               "stale_config_revision");
    }
    if (reviewed->plan.source_revision != fresh.source_revision) {
        receipts.push_back("stale_source_revision");
    }
    if (reviewed->plan.registry_revision != fresh.registry_revision) {
        receipts.push_back("stale_registry_identity");
    }
    if (reviewed->plan.canonical_destination_absent &&
        !fresh.canonical_destination_absent) {
        receipts.push_back("stale_canonical_destination_state");
    }
    receipts.push_back("stale_or_mismatched_plan_hash");
    sort_unique(receipts);
    return receipts;
}

} // namespace

namespace kano::backlog_ops {

bool ProductRegistrationPlan::ready() const {
    return status == "ready" && blockers.empty();
}

std::string ProductRegistrationPlan::to_json(bool pretty) const {
    return json_string(plan_json(*this, true), pretty);
}

std::string ProductRegistrationResult::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["changed_refs"] = string_array(changed_refs);
    value["operation_receipts"] = string_array(operation_receipts);
    value["receipt_ref"] = receipt_ref;
    value["recovery_status"] = recovery_status;
    value["apply_agent"] = nullable_string(apply_agent);
    value["recovery_agent"] = nullable_string(recovery_agent);
    value["idempotent_replay"] = idempotent_replay;
    return json_string(value, pretty);
}

std::string ProductRegistrationVerification::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["postconditions"] = string_array(postconditions);
    value["failures"] = string_array(failures);
    value["apply_agent"] = nullable_string(apply_agent);
    value["recovery_agent"] = nullable_string(recovery_agent);
    return json_string(value, pretty);
}

std::string ProductRegistrationStatus::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["stage"] = stage;
    value["recovery_status"] = recovery_status;
    value["apply_agent"] = nullable_string(apply_agent);
    value["recovery_agent"] = nullable_string(recovery_agent);
    value["rollback_supported"] = rollback_supported;
    return json_string(value, pretty);
}

ProductRegistrationPlan ProductRegistrationOps::plan(
    const PlanOptions& options
) {
    auto prepared = build_prepared(options);
    cache_plan(prepared.plan);
    return prepared.plan;
}

ProductRegistrationResult ProductRegistrationOps::apply(
    const ApplyOptions& options
) {
    ProductRegistrationResult result;
    result.status = "blocked";
    result.plan_hash = options.expected_plan_hash;
    result.recovery_status = "none";
    if (!options.agent) {
        result.operation_receipts.push_back("agent_required");
        return result;
    }
    if (!is_valid_agent(*options.agent)) {
        result.operation_receipts.push_back("invalid_agent");
        return result;
    }
    result.apply_agent = *options.agent;
    if (!options.confirm) {
        result.operation_receipts.push_back("confirmation_required");
        return result;
    }
    if (!is_lowercase_sha256(options.expected_plan_hash)) {
        result.operation_receipts.push_back(
            "invalid_product_registration_plan_hash");
        return result;
    }
    if (options.plan.backlog_root.empty() ||
        !options.plan.backlog_root.is_absolute()) {
        result.operation_receipts.push_back(
            "backlog_root_must_be_absolute");
        return result;
    }

    RecoveryOptions recovery{
        .backlog_root = options.plan.backlog_root,
        .plan_hash = options.expected_plan_hash,
    };
    EvidencePaths paths;
    try {
        paths = evidence_paths(recovery);
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(bounded_error(error.what()));
        return result;
    }

    const auto interruption_requested = [&](std::string_view phase) {
        return options.inject_interruption_after &&
               *options.inject_interruption_after == phase;
    };
    const auto failure_requested = [&](std::string_view phase) {
        return options.inject_failure_after &&
               *options.inject_failure_after == phase;
    };
    const auto exit_if_requested = [&](std::string_view phase) {
        if (options.inject_process_exit_after &&
            *options.inject_process_exit_after == phase) {
            std::_Exit(options.injected_process_exit_code);
        }
    };
    const auto set_interrupted = [&](std::string_view phase,
                                     bool evidence_published) {
        result.status = "recovery_required";
        result.recovery_status = "exact_apply_required";
        if (evidence_published) {
            result.receipt_ref = receipt_ref(options.expected_plan_hash);
        }
        result.operation_receipts.push_back(
            "recoverable_interruption:" + std::string(phase));
    };

    if (std::filesystem::is_regular_file(paths.journal)) {
        try {
            auto evidence = load_validated_evidence(recovery);
            set_recovery_result_actor(result, evidence);
            result.receipt_ref = receipt_ref(options.expected_plan_hash);
            if (!options_match_evidence(options.plan, evidence)) {
                result.operation_receipts.push_back(
                    "stale_or_mismatched_plan_hash");
                return result;
            }
            auto state = evidence.journal["status"].asString();
            if (state == "applied") {
                result.status = "applied";
                result.recovery_status = "not_required";
                result.idempotent_replay = true;
                result.operation_receipts = {
                    "idempotent_replay",
                    "terminal_registration_journal_preserved",
                };
                return result;
            }
            if (state == "failed" &&
                evidence.journal["recovery_status"].asString() ==
                    "config_third_state") {
                result.status = "failed";
                result.recovery_status = "config_third_state";
                result.operation_receipts.push_back("config_third_state");
                return result;
            }

            DirectoryLock lock(
                paths.kano_root / "product-registration.config.lock",
                options.expected_plan_hash,
                options.lock_test_checkpoint);
            evidence = load_validated_evidence(recovery);
            set_recovery_result_actor(result, evidence);
            if (!options_match_evidence(options.plan, evidence)) {
                result.operation_receipts.push_back(
                    "stale_or_mismatched_plan_hash");
                return result;
            }
            state = evidence.journal["status"].asString();
            if (state == "applied") {
                result.status = "applied";
                result.recovery_status = "not_required";
                result.idempotent_replay = true;
                result.operation_receipts = {
                    "idempotent_replay",
                    "terminal_registration_journal_preserved",
                };
                return result;
            }
            append_recovery_attempt(evidence.journal, *options.agent);
            evidence.recovery_agent = *options.agent;
            result.recovery_agent = *options.agent;
            evidence.journal["status"] = "recovery_required";
            evidence.journal["stage"] = "recovery_started";
            evidence.journal["recovery_status"] = "exact_apply_required";
            evidence.journal["updated_at"] = current_utc_timestamp();
            write_journal(paths, evidence.journal);

            const auto current = read_file(paths.config_path);
            if (current != evidence.before && current != evidence.after) {
                evidence.journal["status"] = "failed";
                evidence.journal["stage"] = "config_third_state";
                evidence.journal["recovery_status"] =
                    "config_third_state";
                evidence.journal["updated_at"] = current_utc_timestamp();
                finish_latest_attempt(
                    evidence.journal, "config_third_state");
                write_journal(paths, evidence.journal);
                result.status = "failed";
                result.recovery_status = "config_third_state";
                result.operation_receipts.push_back("config_third_state");
                return result;
            }
            try {
                if (current == evidence.before) {
                    auto safety_failures =
                        source_and_destination_failures(evidence);
                    if (safety_failures.empty()) {
                        safety_failures = recovery_registry_failures(
                            evidence,
                            options.plan);
                    }
                    if (!safety_failures.empty()) {
                        finish_latest_attempt(evidence.journal, "blocked");
                        evidence.journal["stage"] =
                            "recovery_preconditions_blocked";
                        evidence.journal["last_error"] =
                            safety_failures.front();
                        write_journal(paths, evidence.journal);
                        result.status = "recovery_required";
                        result.recovery_status = "exact_apply_required";
                        result.operation_receipts = safety_failures;
                        return result;
                    }
                    evidence.journal["stage"] =
                        "recovery_config_publish_pending";
                    evidence.journal["updated_at"] = current_utc_timestamp();
                    write_journal(paths, evidence.journal);
                    replace_file_atomic(
                        paths.config_path,
                        evidence.after,
                        options.expected_plan_hash,
                        "publish");
                    exit_if_requested("after_config_publish");
                    if (interruption_requested("after_config_publish")) {
                        set_interrupted("after_config_publish", true);
                        return result;
                    }
                    if (failure_requested("after_config_publish")) {
                        throw std::runtime_error(
                            "injected_failure:after_config_publish");
                    }
                }
                const auto final_failures =
                    registration_postcondition_failures(evidence);
                if (!final_failures.empty()) {
                    throw std::runtime_error(final_failures.front());
                }
                if (interruption_requested("after_postcondition_check")) {
                    set_interrupted("after_postcondition_check", true);
                    return result;
                }
                if (failure_requested("after_postcondition_check")) {
                    throw std::runtime_error(
                        "injected_failure:after_postcondition_check");
                }
                evidence.journal["status"] = "applied";
                evidence.journal["stage"] = "completed";
                evidence.journal["recovery_status"] = "not_required";
                evidence.journal["applied_at"] = current_utc_timestamp();
                finish_latest_attempt(evidence.journal, "applied");
                write_journal(paths, evidence.journal);
                result.status = "applied";
                result.recovery_status = "completed";
                result.changed_refs = {
                    "project-config:.kano/backlog_config.toml",
                };
                result.operation_receipts = {
                    state == "rolled_back"
                        ? "rolled_back_transaction_retried"
                        : "interrupted_publish_reconciled",
                    "postconditions_verified",
                };
                return result;
            } catch (const std::exception& recovery_error) {
                const auto recovery_current = read_file(paths.config_path);
                if (recovery_current == evidence.after) {
                    replace_file_atomic(
                        paths.config_path,
                        evidence.before,
                        options.expected_plan_hash,
                        "restore");
                } else if (recovery_current != evidence.before) {
                    evidence.journal["status"] = "failed";
                    evidence.journal["stage"] = "config_third_state";
                    evidence.journal["recovery_status"] =
                        "config_third_state";
                    evidence.journal["last_error"] =
                        bounded_error(recovery_error.what());
                    finish_latest_attempt(
                        evidence.journal, "config_third_state");
                    write_journal(paths, evidence.journal);
                    result.status = "failed";
                    result.recovery_status = "config_third_state";
                    result.operation_receipts.push_back(
                        "config_third_state");
                    return result;
                }
                evidence.journal["status"] = "rolled_back";
                evidence.journal["stage"] = "automatic_rollback";
                evidence.journal["recovery_status"] = "completed";
                evidence.journal["rolled_back_at"] =
                    current_utc_timestamp();
                evidence.journal["last_error"] =
                    bounded_error(recovery_error.what());
                finish_latest_attempt(evidence.journal, "rolled_back");
                write_journal(paths, evidence.journal);
                result.status = "rolled_back";
                result.recovery_status = "completed";
                result.operation_receipts = {
                    bounded_error(recovery_error.what()),
                    "automatic_rollback_completed",
                };
                sort_unique(result.operation_receipts);
                return result;
            }
        } catch (const std::exception& error) {
            result.operation_receipts.push_back(bounded_error(error.what()));
            result.recovery_status = "exact_apply_required";
            return result;
        }
    }

    auto prepared = build_prepared(options.plan);
    result.plan_hash = options.expected_plan_hash;
    if (!prepared.plan.ready() ||
        prepared.plan.plan_hash != options.expected_plan_hash) {
        if (prepared.plan.plan_hash == options.expected_plan_hash) {
            result.operation_receipts = prepared.plan.blockers;
            result.operation_receipts.push_back("plan_not_ready");
        } else {
            result.operation_receipts = stale_receipts(
                prepared.plan,
                cached_plan(options.expected_plan_hash));
        }
        sort_unique(result.operation_receipts);
        return result;
    }

    Json::Value journal;
    bool transaction_started = false;
    std::unique_ptr<DirectoryLock> config_lock;
    try {
        config_lock = std::make_unique<DirectoryLock>(
            paths.kano_root / "product-registration.config.lock",
            prepared.plan.plan_hash,
            options.lock_test_checkpoint);
        paths = evidence_paths(recovery);
        if (std::filesystem::is_regular_file(paths.journal)) {
            result.status = "recovery_required";
            result.recovery_status = "exact_apply_required";
            result.receipt_ref = receipt_ref(options.expected_plan_hash);
            result.operation_receipts.push_back(
                "transaction_published_retry_exact_apply");
            return result;
        }
        const auto locked = build_prepared(options.plan);
        if (!locked.plan.ready() ||
            locked.plan.plan_hash != options.expected_plan_hash) {
            result.operation_receipts = stale_receipts(
                locked.plan,
                cached_plan(options.expected_plan_hash));
            sort_unique(result.operation_receipts);
            return result;
        }
        prepared = locked;
        ensure_registration_cache_authority(paths);
        reclaim_same_plan_staging(paths, prepared.plan.plan_hash);
        if (std::filesystem::exists(paths.transaction)) {
            throw std::runtime_error("transaction_evidence_incomplete");
        }

        const auto receipt = make_receipt(prepared, *options.agent);
        const auto receipt_bytes = json_string(receipt, true);
        journal = make_journal(prepared, *options.agent, receipt_bytes);
        const auto staged = create_staged_evidence(
            paths, prepared, *options.agent, receipt_bytes, journal);

        exit_if_requested("after_staged_evidence");
        if (interruption_requested("after_staged_evidence")) {
            set_interrupted("after_staged_evidence", false);
            return result;
        }
        if (failure_requested("after_staged_evidence")) {
            throw std::runtime_error(
                "injected_failure:after_staged_evidence");
        }

        publish_staged_evidence(paths, staged);
        transaction_started = true;
        result.receipt_ref = receipt_ref(prepared.plan.plan_hash);

        exit_if_requested("after_transaction_publish");
        if (interruption_requested("after_transaction_publish")) {
            set_interrupted("after_transaction_publish", true);
            return result;
        }
        if (failure_requested("after_transaction_publish")) {
            throw std::runtime_error(
                "injected_failure:after_transaction_publish");
        }

        journal["status"] = "publishing";
        journal["stage"] = "config_publish_pending";
        journal["recovery_status"] = "exact_apply_required";
        journal["updated_at"] = current_utc_timestamp();
        write_journal(paths, journal);
        const auto current_config = read_file(prepared.config_path);
        if (current_config != prepared.config_before ||
            sha256_hex(current_config) != prepared.plan.config_revision) {
            throw std::runtime_error(
                "concurrent_config_drift_before_publish");
        }
        const auto locked_again = build_prepared(options.plan);
        if (!locked_again.plan.ready() ||
            locked_again.plan.plan_hash != options.expected_plan_hash) {
            throw std::runtime_error("concurrent_registration_drift");
        }
        replace_file_atomic(
            prepared.config_path,
            prepared.config_after,
            prepared.plan.plan_hash,
            "publish");

        exit_if_requested("after_config_publish");
        if (interruption_requested("after_config_publish")) {
            set_interrupted("after_config_publish", true);
            return result;
        }
        if (failure_requested("after_config_publish")) {
            throw std::runtime_error(
                "injected_failure:after_config_publish");
        }

        journal["stage"] = "after_config_publish";
        journal["updated_at"] = current_utc_timestamp();
        write_journal(paths, journal);
        const auto evidence = load_validated_evidence(recovery);
        const auto failures = registration_postcondition_failures(evidence);
        if (!failures.empty()) {
            throw std::runtime_error(failures.front());
        }

        if (interruption_requested("after_postcondition_check")) {
            set_interrupted("after_postcondition_check", true);
            return result;
        }
        if (failure_requested("after_postcondition_check")) {
            throw std::runtime_error(
                "injected_failure:after_postcondition_check");
        }

        journal = evidence.journal;
        journal["status"] = "applied";
        journal["stage"] = "completed";
        journal["recovery_status"] = "not_required";
        journal["applied_at"] = current_utc_timestamp();
        finish_latest_attempt(journal, "applied");
        write_journal(paths, journal);

        result.status = "applied";
        result.recovery_status = "not_required";
        result.changed_refs = {prepared.plan.config_ref};
        result.operation_receipts = {
            "config_registration_published",
            "config_publish_atomic",
            "evidence_published_atomically",
            "external_root_preserved_read_only",
            "reviewed_plan_hash_matched",
            "postconditions_verified",
        };
        sort_unique(result.operation_receipts);
        return result;
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(bounded_error(error.what()));
        if (!transaction_started ||
            !std::filesystem::is_regular_file(paths.journal)) {
            result.status = "recovery_required";
            result.recovery_status = "exact_apply_required";
            sort_unique(result.operation_receipts);
            return result;
        }
        try {
            if (!config_lock) {
                config_lock = std::make_unique<DirectoryLock>(
                    paths.kano_root / "product-registration.config.lock",
                    options.expected_plan_hash,
                    options.lock_test_checkpoint);
            }
            auto evidence = load_validated_evidence(recovery);
            append_recovery_attempt(evidence.journal, *options.agent);
            evidence.recovery_agent = *options.agent;
            result.recovery_agent = *options.agent;
            evidence.journal["status"] = "recovery_required";
            evidence.journal["stage"] = "automatic_rollback_started";
            evidence.journal["recovery_status"] = "exact_apply_required";
            write_journal(paths, evidence.journal);
            const auto current = read_file(paths.config_path);
            if (current == evidence.after) {
                replace_file_atomic(
                    paths.config_path,
                    evidence.before,
                    options.expected_plan_hash,
                    "restore");
            } else if (current != evidence.before) {
                evidence.journal["status"] = "failed";
                evidence.journal["stage"] = "config_third_state";
                evidence.journal["recovery_status"] =
                    "config_third_state";
                evidence.journal["last_error"] = bounded_error(error.what());
                finish_latest_attempt(
                    evidence.journal, "config_third_state");
                write_journal(paths, evidence.journal);
                result.status = "failed";
                result.recovery_status = "config_third_state";
                result.operation_receipts.push_back("config_third_state");
                sort_unique(result.operation_receipts);
                return result;
            }
            evidence.journal["status"] = "rolled_back";
            evidence.journal["stage"] = "automatic_rollback";
            evidence.journal["recovery_status"] = "completed";
            evidence.journal["rolled_back_at"] = current_utc_timestamp();
            evidence.journal["last_error"] = bounded_error(error.what());
            finish_latest_attempt(evidence.journal, "rolled_back");
            write_journal(paths, evidence.journal);
            result.status = "rolled_back";
            result.recovery_status = "completed";
            result.receipt_ref = receipt_ref(options.expected_plan_hash);
            result.operation_receipts.push_back(
                "automatic_rollback_completed");
        } catch (const std::exception& recovery_error) {
            result.status = "recovery_required";
            result.recovery_status = "exact_apply_required";
            result.operation_receipts.push_back(
                "automatic_rollback_failed:" +
                bounded_error(recovery_error.what()));
        }
        sort_unique(result.operation_receipts);
        return result;
    }
}

ProductRegistrationVerification ProductRegistrationOps::verify(
    const RecoveryOptions& options
) {
    ProductRegistrationVerification verification;
    verification.status = "not_applied";
    verification.plan_hash = options.plan_hash;
    try {
        const auto evidence = load_validated_evidence(options);
        verification.apply_agent = evidence.apply_agent;
        verification.recovery_agent = evidence.recovery_agent;
        const auto state = evidence.journal["status"].asString();
        if (state == "rolled_back") {
            verification.failures.push_back("registration_rolled_back");
            return verification;
        }
        if (state != "applied") {
            verification.status =
                state == "prepared" || state == "publishing" ||
                        state == "recovery_required"
                    ? "recovery_required"
                    : "failed";
            verification.failures.push_back(
                "registration_not_applied:" + state);
            return verification;
        }
        verification.failures =
            registration_postcondition_failures(evidence);
        if (verification.failures.empty()) {
            verification.postconditions = {
                "canonical_destination_absent",
                "config_registration_exact",
                "external_root_resolves_exactly",
                "source_bytes_unchanged",
            };
            verification.status = "verified";
        } else {
            verification.status = "failed";
        }
    } catch (const std::exception& error) {
        verification.failures.push_back(bounded_error(error.what()));
        verification.status =
            std::string(error.what()) ==
                    "product_registration_journal_not_found"
                ? "not_applied"
                : "failed";
    }
    sort_unique(verification.failures);
    sort_unique(verification.postconditions);
    return verification;
}

ProductRegistrationStatus ProductRegistrationOps::status(
    const RecoveryOptions& options
) {
    ProductRegistrationStatus result;
    result.status = "not_applied";
    result.plan_hash = options.plan_hash;
    result.recovery_status = "none";
    result.rollback_supported = false;
    try {
        const auto evidence = load_validated_evidence(options);
        result.apply_agent = evidence.apply_agent;
        result.recovery_agent = evidence.recovery_agent;
        result.status = evidence.journal["status"].asString();
        result.stage = evidence.journal["stage"].asString();
        const auto current = read_file(evidence.paths.config_path);
        const bool nonterminal =
            result.status == "prepared" || result.status == "publishing" ||
            result.status == "recovery_required";
        if (nonterminal &&
            current != evidence.before && current != evidence.after) {
            result.status = "failed";
            result.stage = "config_third_state";
            result.recovery_status = "config_third_state";
        } else if (nonterminal) {
            result.status = "recovery_required";
            if (current == evidence.after) {
                result.stage = "after_config_publish";
            }
            result.recovery_status = "exact_apply_required";
        } else if (result.status == "applied") {
            result.stage = "completed";
            result.recovery_status = "not_required";
        } else if (result.status == "rolled_back") {
            result.recovery_status = "completed";
        } else if (result.status == "failed") {
            result.recovery_status =
                evidence.journal["recovery_status"].asString();
        }
    } catch (const std::exception& error) {
        if (std::string(error.what()) !=
            "product_registration_journal_not_found") {
            result.status = "failed";
            result.recovery_status = bounded_error(error.what());
        }
    }
    return result;
}

} // namespace kano::backlog_ops
