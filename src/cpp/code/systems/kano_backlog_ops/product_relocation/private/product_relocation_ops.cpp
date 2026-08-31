#include "kano/backlog_ops/product_relocation/product_relocation_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"

#include <json/json.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
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
#include <unistd.h>
#endif

namespace {

using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ConfigLoader;
using kano::backlog_core::ProjectConfig;
using kano::backlog_core::ProductResolution;
using kano::backlog_core::ProductResolutionKind;
using kano::backlog_ops::ProductRelocationFile;
using kano::backlog_ops::ProductRelocationIdentity;
using kano::backlog_ops::ProductRelocationPlan;
using kano::backlog_ops::ProductRelocationRequest;

constexpr std::uintmax_t kMaximumSingleFileBytes =
    256ull * 1024ull * 1024ull;
constexpr const char* kJournalSchema =
    "kob.product_root_relocation.journal.v1";

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

bool is_within_or_equal(
    const std::filesystem::path& child,
    const std::filesystem::path& parent
) {
    if (child == parent) {
        return true;
    }
    const auto relative = child.lexically_relative(parent);
    return !relative.empty() && !relative.is_absolute() &&
           relative.begin()->generic_string() != "..";
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

std::string bounded_error(std::string message) {
    static const std::regex windows_path("[A-Za-z]:[\\\\/]");
    if (std::regex_search(message, windows_path) ||
        message.find('\\') != std::string::npos ||
        message.starts_with("/") ||
        message.find(":/") != std::string::npos) {
        return "relocation_operation_failed:details_redacted";
    }
    return message;
}

template <typename T>
void sort_unique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
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

void write_file_atomic(
    const std::filesystem::path& path,
    const std::string& content
) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".kob-product-relocation.tmp";
    if (std::filesystem::exists(temporary)) {
        throw std::runtime_error("atomic_stage_already_exists");
    }
    {
        std::ofstream output(
            temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("atomic_stage_open_failed");
        }
        output.write(
            content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output.good()) {
            throw std::runtime_error("atomic_stage_write_failed");
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic_publish_failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic_publish_failed");
    }
#endif
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

bool is_sha256(const std::string& value) {
    return value.size() == 64u &&
           std::all_of(
               value.begin(), value.end(), [](unsigned char ch) {
                   return std::isxdigit(ch) != 0;
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
    auto rendered = Json::writeString(builder, value);
    if (!pretty) {
        return rendered;
    }
    std::string normalized;
    normalized.reserve(rendered.size());
    std::size_t line_start = 0;
    while (line_start < rendered.size()) {
        const auto newline = rendered.find('\n', line_start);
        const auto line_end =
            newline == std::string::npos ? rendered.size() : newline;
        auto content_end = line_end;
        while (content_end > line_start &&
               (rendered[content_end - 1] == ' ' ||
                rendered[content_end - 1] == '\t')) {
            --content_end;
        }
        normalized.append(
            rendered, line_start, content_end - line_start);
        if (newline == std::string::npos) {
            break;
        }
        normalized.push_back('\n');
        line_start = newline + 1;
    }
    return normalized;
}

Json::Value parse_json(const std::string& content) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value value;
    std::string errors;
    std::istringstream input(content);
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("invalid_relocation_journal_json");
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

Json::Value request_json(const ProductRelocationRequest& request) {
    Json::Value value(Json::objectValue);
    value["product"] = request.product;
    value["destination_root_supplied"] =
        !request.destination_root.empty();
    value["expected_source_revision"] =
        request.expected_source_revision.value_or("");
    value["max_files"] = static_cast<Json::UInt64>(request.max_files);
    value["max_bytes"] = static_cast<Json::UInt64>(request.max_bytes);
    value["max_items"] = static_cast<Json::UInt64>(request.max_items);
    return value;
}

Json::Value plan_json(
    const ProductRelocationPlan& plan,
    bool include_hash
) {
    Json::Value value(Json::objectValue);
    value["schema"] = plan.schema;
    value["status"] = plan.status;
    value["request"] = request_json(plan.request);
    value["product"] = plan.product;
    value["prefix"] = plan.prefix;
    value["source_root_ref"] = plan.source_root_ref;
    value["destination_root_ref"] = plan.destination_root_ref;
    value["config_ref"] = plan.config_ref;
    value["destination_path_digest"] = plan.destination_path_digest;
    value["source_revision"] = plan.source_revision;
    value["config_revision"] = plan.config_revision;
    value["sequence_state_revision"] = plan.sequence_state_revision;
    value["sequence_count"] =
        static_cast<Json::UInt64>(plan.sequence_count);
    value["reservation_count"] =
        static_cast<Json::UInt64>(plan.reservation_count);
    value["relocation_strategy"] = plan.relocation_strategy;
    value["cross_volume_safe"] = plan.cross_volume_safe;
    value["destination_preexisted_empty"] =
        plan.destination_preexisted_empty;

    Json::Value files(Json::arrayValue);
    for (const auto& file : plan.files) {
        Json::Value entry(Json::objectValue);
        entry["ref"] = file.ref;
        entry["kind"] = file.kind;
        entry["size"] = static_cast<Json::UInt64>(file.size);
        entry["sha256"] = file.sha256;
        entry["preserves_bytes"] = file.preserves_bytes;
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
    value["reference_checks"] = string_array(plan.reference_checks);
    value["derived_surfaces"] = string_array(plan.derived_surfaces);
    value["validation_steps"] = string_array(plan.validation_steps);
    value["blockers"] = string_array(plan.blockers);
    value["warnings"] = string_array(plan.warnings);
    value["plan_hash"] = include_hash ? plan.plan_hash : "";
    value["dry_run"] = plan.dry_run;
    value["mutates_backlog"] = plan.mutates_backlog;
    return value;
}

void add_blocker(ProductRelocationPlan& plan, std::string blocker) {
    plan.blockers.push_back(std::move(blocker));
    plan.status = "blocked";
}

std::string bounded_product_ref(
    const std::string& product,
    const std::string& relative
) {
    return "product:" + product + "/" + relative;
}

std::string first_component(const std::filesystem::path& relative) {
    const auto iterator = relative.begin();
    return iterator == relative.end() ? std::string{} :
        iterator->generic_string();
}

bool is_derived_relative(const std::filesystem::path& relative) {
    const auto top = first_component(relative);
    const auto filename = relative.filename().generic_string();
    return top == ".cache" || top == "views" || top == "_views" ||
           filename.find(".index.md") != std::string::npos;
}

std::string derived_ref(
    const std::string& product,
    const std::filesystem::path& relative
) {
    const auto generic = relative.generic_string();
    if (first_component(relative) == ".cache") {
        const auto suffix =
            relative.lexically_relative(".cache").generic_string();
        return "product-cache:" + product + "/" + suffix;
    }
    return "product-derived:" + product + "/" + generic;
}

std::string classify_canonical_file(
    const std::filesystem::path& relative
) {
    const auto top = first_component(relative);
    if (top == "items" || top == "_trash") {
        return "canonical_item";
    }
    if (top == "artifacts") {
        return "evidence_or_artifact";
    }
    if (top == "_meta") {
        return "canonical_audit_metadata";
    }
    return "canonical_product_file";
}

bool looks_like_raw_path(const std::string& value) {
    const auto trimmed = trim_copy(value);
    if (trimmed.empty() || trimmed.find("://") != std::string::npos) {
        return false;
    }
    if (trimmed.starts_with("\\\\") ||
        trimmed.starts_with("//") ||
        (trimmed.size() > 2 &&
         std::isalpha(static_cast<unsigned char>(trimmed[0])) &&
         trimmed[1] == ':' &&
         (trimmed[2] == '\\' || trimmed[2] == '/'))) {
        return true;
    }
    return std::filesystem::path(trimmed).is_absolute();
}

bool looks_like_item_id(const std::string& value) {
    static const std::regex pattern(
        "^[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4}$");
    return std::regex_match(value, pattern);
}

std::vector<std::string> structured_refs(const BacklogItem& item) {
    std::vector<std::string> refs;
    const auto append_optional =
        [&](const std::optional<std::string>& value) {
            if (value && !value->empty()) {
                refs.push_back(*value);
            }
        };
    const auto append_vector =
        [&](const std::vector<std::string>& values) {
            refs.insert(refs.end(), values.begin(), values.end());
        };
    append_optional(item.parent);
    append_optional(item.duplicate_of);
    append_vector(item.links.relates);
    append_vector(item.links.blocks);
    append_vector(item.links.blocked_by);
    append_vector(item.decisions);
    append_vector(item.intent_provenance_refs);
    append_vector(item.intent_conflicts_with);
    append_vector(item.intent_supersedes);
    for (const auto& [key, value] : item.external) {
        (void)key;
        if (!value.empty()) {
            refs.push_back(value);
        }
    }
    return refs;
}

std::string comment_suffix(const std::string& value) {
    bool quoted = false;
    char quote = '\0';
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto ch = value[index];
        if (quoted) {
            if (ch == '\\') {
                ++index;
            } else if (ch == quote) {
                quoted = false;
            }
        } else if (ch == '\'' || ch == '"') {
            quoted = true;
            quote = ch;
        } else if (ch == '#') {
            return value.substr(index);
        }
    }
    return {};
}

std::string rewrite_product_root(
    const std::string& content,
    const std::string& product,
    const std::string& target
) {
    const std::set<std::string> accepted_sections = {
        "[products." + product + "]",
        "[products.\"" + product + "\"]",
    };
    bool in_product = false;
    bool found_section = false;
    std::size_t replacements = 0;
    std::string output;
    std::size_t cursor = 0;
    while (cursor < content.size()) {
        const auto newline = content.find('\n', cursor);
        const auto end =
            newline == std::string::npos ? content.size() : newline;
        auto line = content.substr(cursor, end - cursor);
        const bool carriage_return = !line.empty() && line.back() == '\r';
        if (carriage_return) {
            line.pop_back();
        }
        const auto trimmed = trim_copy(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            in_product = accepted_sections.contains(trimmed);
            found_section = found_section || in_product;
        } else if (in_product && !trimmed.starts_with("#")) {
            const auto equals = line.find('=');
            if (equals != std::string::npos &&
                trim_copy(line.substr(0, equals)) == "backlog_root") {
                const auto after = line.substr(equals + 1);
                const auto comment = comment_suffix(after);
                line = line.substr(0, equals + 1) + " \"" + target + "\"" +
                       (comment.empty() ? "" : " " + comment);
                ++replacements;
            }
        }
        output += line;
        if (carriage_return) {
            output.push_back('\r');
        }
        if (newline != std::string::npos) {
            output.push_back('\n');
            cursor = newline + 1;
        } else {
            cursor = content.size();
        }
    }
    if (!found_section) {
        throw std::runtime_error("product_config_section_not_found");
    }
    if (replacements != 1) {
        throw std::runtime_error("product_backlog_root_not_unique");
    }
    return output;
}

struct SequenceRow {
    std::string prefix;
    std::string type_code;
    std::int64_t next_number = 0;
};

struct ReservationRow {
    std::string prefix;
    std::string type_code;
    std::int64_t number = 0;
    std::string owner;
    std::int64_t created_at = 0;
    std::optional<std::int64_t> committed_at;
};

struct SequenceState {
    bool source_database_exists = false;
    std::vector<SequenceRow> sequences;
    std::vector<ReservationRow> reservations;
    std::string revision;
};

std::string sqlite_path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

class SqliteHandle {
public:
    SqliteHandle(const std::filesystem::path& path, int flags) {
        if (sqlite3_open_v2(
                sqlite_path(path).c_str(), &database_, flags, nullptr) !=
            SQLITE_OK) {
            if (database_) {
                sqlite3_close(database_);
                database_ = nullptr;
            }
            throw std::runtime_error("sequence_database_open_failed");
        }
    }

    ~SqliteHandle() {
        if (database_) {
            sqlite3_close(database_);
        }
    }

    SqliteHandle(const SqliteHandle&) = delete;
    SqliteHandle& operator=(const SqliteHandle&) = delete;

    sqlite3* get() const {
        return database_;
    }

private:
    sqlite3* database_ = nullptr;
};

bool sqlite_table_exists(sqlite3* database, const char* table) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
            -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error("sequence_database_query_failed");
    }
    sqlite3_bind_text(statement, 1, table, -1, SQLITE_STATIC);
    const bool exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return exists;
}

Json::Value sequence_state_json(const SequenceState& state) {
    Json::Value value(Json::objectValue);
    value["source_database_exists"] = state.source_database_exists;
    Json::Value sequences(Json::arrayValue);
    for (const auto& row : state.sequences) {
        Json::Value entry(Json::objectValue);
        entry["prefix"] = row.prefix;
        entry["type_code"] = row.type_code;
        entry["next_number"] = static_cast<Json::Int64>(row.next_number);
        sequences.append(entry);
    }
    value["sequences"] = sequences;
    Json::Value reservations(Json::arrayValue);
    for (const auto& row : state.reservations) {
        Json::Value entry(Json::objectValue);
        entry["prefix"] = row.prefix;
        entry["type_code"] = row.type_code;
        entry["number"] = static_cast<Json::Int64>(row.number);
        entry["owner"] = row.owner;
        entry["created_at"] = static_cast<Json::Int64>(row.created_at);
        if (row.committed_at) {
            entry["committed_at"] =
                static_cast<Json::Int64>(*row.committed_at);
        } else {
            entry["committed_at"] = Json::nullValue;
        }
        reservations.append(entry);
    }
    value["reservations"] = reservations;
    return value;
}

SequenceState sequence_state_from_json(const Json::Value& value) {
    SequenceState state;
    state.source_database_exists =
        value["source_database_exists"].asBool();
    for (const auto& entry : value["sequences"]) {
        state.sequences.push_back({
            entry["prefix"].asString(),
            entry["type_code"].asString(),
            entry["next_number"].asInt64(),
        });
    }
    for (const auto& entry : value["reservations"]) {
        ReservationRow row;
        row.prefix = entry["prefix"].asString();
        row.type_code = entry["type_code"].asString();
        row.number = entry["number"].asInt64();
        row.owner = entry["owner"].asString();
        row.created_at = entry["created_at"].asInt64();
        if (!entry["committed_at"].isNull()) {
            row.committed_at = entry["committed_at"].asInt64();
        }
        state.reservations.push_back(std::move(row));
    }
    state.revision = sha256_hex(json_string(value, false));
    return state;
}

SequenceState read_sequence_state(
    const std::filesystem::path& source_root,
    ProductRelocationPlan& plan
) {
    SequenceState state;
    const auto database =
        source_root / ".cache" / "index" / "backlog.db";
    const auto wal = std::filesystem::path(database.string() + "-wal");
    const auto shm = std::filesystem::path(database.string() + "-shm");
    if (std::filesystem::exists(wal) || std::filesystem::exists(shm)) {
        add_blocker(plan, "source_index_has_live_wal_or_shm");
        return state;
    }
    if (!std::filesystem::is_regular_file(database)) {
        state.revision = sha256_hex(
            json_string(sequence_state_json(state), false));
        return state;
    }
    state.source_database_exists = true;
    SqliteHandle handle(database, SQLITE_OPEN_READONLY);
    if (sqlite_table_exists(handle.get(), "id_sequences")) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(
                handle.get(),
                "SELECT prefix,type_code,next_number FROM id_sequences "
                "ORDER BY prefix,type_code",
                -1, &statement, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sequence_database_query_failed");
        }
        while (sqlite3_step(statement) == SQLITE_ROW) {
            state.sequences.push_back({
                reinterpret_cast<const char*>(
                    sqlite3_column_text(statement, 0)),
                reinterpret_cast<const char*>(
                    sqlite3_column_text(statement, 1)),
                sqlite3_column_int64(statement, 2),
            });
        }
        sqlite3_finalize(statement);
    }
    if (sqlite_table_exists(handle.get(), "id_reservations")) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(
                handle.get(),
                "SELECT prefix,type_code,number,owner,created_at,committed_at "
                "FROM id_reservations ORDER BY prefix,type_code,number",
                -1, &statement, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sequence_database_query_failed");
        }
        while (sqlite3_step(statement) == SQLITE_ROW) {
            ReservationRow row;
            row.prefix = reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 0));
            row.type_code = reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 1));
            row.number = sqlite3_column_int64(statement, 2);
            row.owner = reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 3));
            row.created_at = sqlite3_column_int64(statement, 4);
            if (sqlite3_column_type(statement, 5) != SQLITE_NULL) {
                row.committed_at = sqlite3_column_int64(statement, 5);
            }
            state.reservations.push_back(std::move(row));
        }
        sqlite3_finalize(statement);
    }
    state.revision = sha256_hex(
        json_string(sequence_state_json(state), false));
    return state;
}

void sqlite_execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &message) !=
        SQLITE_OK) {
        sqlite3_free(message);
        throw std::runtime_error("sequence_database_write_failed");
    }
}

void restore_sequence_state(
    const std::filesystem::path& database,
    const SequenceState& state
) {
    SqliteHandle handle(
        database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    sqlite_execute(handle.get(), "BEGIN IMMEDIATE");
    try {
        sqlite_execute(handle.get(), "DELETE FROM id_sequences");
        sqlite_execute(handle.get(), "DELETE FROM id_reservations");
        sqlite3_stmt* sequence = nullptr;
        if (sqlite3_prepare_v2(
                handle.get(),
                "INSERT INTO id_sequences(prefix,type_code,next_number) "
                "VALUES(?,?,?)",
                -1, &sequence, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sequence_database_write_failed");
        }
        for (const auto& row : state.sequences) {
            sqlite3_bind_text(
                sequence, 1, row.prefix.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(
                sequence, 2, row.type_code.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(sequence, 3, row.next_number);
            if (sqlite3_step(sequence) != SQLITE_DONE) {
                sqlite3_finalize(sequence);
                throw std::runtime_error("sequence_database_write_failed");
            }
            sqlite3_reset(sequence);
            sqlite3_clear_bindings(sequence);
        }
        sqlite3_finalize(sequence);

        sqlite3_stmt* reservation = nullptr;
        if (sqlite3_prepare_v2(
                handle.get(),
                "INSERT INTO id_reservations("
                "prefix,type_code,number,owner,created_at,committed_at) "
                "VALUES(?,?,?,?,?,?)",
                -1, &reservation, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sequence_database_write_failed");
        }
        for (const auto& row : state.reservations) {
            sqlite3_bind_text(
                reservation, 1, row.prefix.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(
                reservation, 2, row.type_code.c_str(), -1,
                SQLITE_TRANSIENT);
            sqlite3_bind_int64(reservation, 3, row.number);
            sqlite3_bind_text(
                reservation, 4, row.owner.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(reservation, 5, row.created_at);
            if (row.committed_at) {
                sqlite3_bind_int64(
                    reservation, 6, *row.committed_at);
            } else {
                sqlite3_bind_null(reservation, 6);
            }
            if (sqlite3_step(reservation) != SQLITE_DONE) {
                sqlite3_finalize(reservation);
                throw std::runtime_error("sequence_database_write_failed");
            }
            sqlite3_reset(reservation);
            sqlite3_clear_bindings(reservation);
        }
        sqlite3_finalize(reservation);
        sqlite_execute(handle.get(), "COMMIT");
    } catch (...) {
        try {
            sqlite_execute(handle.get(), "ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

bool sequence_states_equal(
    const SequenceState& left,
    const SequenceState& right
) {
    return left.source_database_exists == right.source_database_exists &&
           json_string(sequence_state_json(left), false) ==
               json_string(sequence_state_json(right), false);
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

struct PreparedRelocation {
    ProductRelocationPlan plan;
    std::filesystem::path config_root;
    std::filesystem::path config_path;
    std::filesystem::path source_root;
    std::filesystem::path destination_root;
    std::string config_before;
    std::string config_after;
    std::vector<ManifestEntry> manifest;
    std::vector<ParsedIdentity> parsed_identities;
    SequenceState sequence_state;
};

void finalize_plan(PreparedRelocation& prepared) {
    std::sort(
        prepared.plan.files.begin(), prepared.plan.files.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.ref, left.kind) <
                   std::tie(right.ref, right.kind);
        });
    std::sort(
        prepared.plan.identities.begin(),
        prepared.plan.identities.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.id, left.uid, left.source_ref) <
                   std::tie(right.id, right.uid, right.source_ref);
        });
    std::sort(
        prepared.manifest.begin(), prepared.manifest.end(),
        [](const auto& left, const auto& right) {
            return left.relative < right.relative;
        });
    sort_unique(prepared.plan.reference_checks);
    sort_unique(prepared.plan.derived_surfaces);
    sort_unique(prepared.plan.validation_steps);
    sort_unique(prepared.plan.blockers);
    sort_unique(prepared.plan.warnings);
    prepared.plan.status =
        prepared.plan.blockers.empty() ? "ready" : "blocked";
    prepared.plan.plan_hash = sha256_hex(
        json_string(plan_json(prepared.plan, false), false));
}

void inventory_source(PreparedRelocation& prepared) {
    std::uintmax_t total_bytes = 0;
    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        prepared.source_root,
        std::filesystem::directory_options::none,
        iterator_error);
    if (iterator_error) {
        add_blocker(prepared.plan, "source_inventory_open_failed");
        return;
    }
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            add_blocker(prepared.plan, "source_inventory_read_failed");
            return;
        }
        const auto& entry = *iterator;
        if (entry.is_symlink()) {
            add_blocker(prepared.plan, "source_symlink_not_supported");
            if (entry.is_directory()) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto relative =
            entry.path().lexically_relative(prepared.source_root);
        if (relative.empty() || relative.is_absolute() ||
            first_component(relative) == "..") {
            add_blocker(prepared.plan, "source_path_escape_detected");
            continue;
        }
        const auto generic = relative.generic_string();
        if (generic == ".cache/index/backlog.db-wal" ||
            generic == ".cache/index/backlog.db-shm") {
            add_blocker(
                prepared.plan, "source_index_has_live_wal_or_shm");
        }
        if (is_derived_relative(relative)) {
            prepared.plan.derived_surfaces.push_back(
                derived_ref(prepared.plan.product, relative));
            continue;
        }
        if (prepared.manifest.size() >=
            prepared.plan.request.max_files) {
            add_blocker(prepared.plan, "source_file_limit_exceeded");
            return;
        }
        FileDigest digest;
        try {
            digest = digest_file(entry.path());
        } catch (const std::exception& error) {
            add_blocker(
                prepared.plan,
                "source_file_unreadable:" + generic + ":" +
                    error.what());
            continue;
        }
        if (digest.size >
            prepared.plan.request.max_bytes - total_bytes) {
            add_blocker(prepared.plan, "source_byte_limit_exceeded");
            return;
        }
        total_bytes += digest.size;
        const auto kind = classify_canonical_file(relative);
        prepared.manifest.push_back(
            {generic, kind, digest.size, digest.sha256});
        prepared.plan.files.push_back(ProductRelocationFile{
            bounded_product_ref(prepared.plan.product, generic),
            kind,
            digest.size,
            digest.sha256,
            true,
        });
    }
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

std::optional<ManifestEntry> manifest_entry(
    const PreparedRelocation& prepared,
    const std::filesystem::path& path
) {
    const auto relative =
        path.lexically_relative(prepared.source_root).generic_string();
    const auto found = std::lower_bound(
        prepared.manifest.begin(), prepared.manifest.end(), relative,
        [](const auto& entry, const auto& value) {
            return entry.relative < value;
        });
    if (found == prepared.manifest.end() ||
        found->relative != relative) {
        return std::nullopt;
    }
    return *found;
}

void collect_source_identities(PreparedRelocation& prepared) {
    CanonicalStore store(prepared.source_root);
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> uids;
    std::size_t item_count = 0;
    for (const auto& path : store.list_items()) {
        const auto relative = path.lexically_relative(prepared.source_root);
        if (is_derived_relative(relative)) {
            continue;
        }
        if (++item_count > prepared.plan.request.max_items) {
            add_blocker(prepared.plan, "source_item_limit_exceeded");
            return;
        }
        const auto manifest = manifest_entry(prepared, path);
        if (!manifest) {
            add_blocker(
                prepared.plan,
                "canonical_item_missing_from_manifest:" +
                    bounded_product_ref(
                        prepared.plan.product,
                        relative.generic_string()));
            continue;
        }
        try {
            auto item = store.read(path);
            if (!ids.insert(item.id).second) {
                add_blocker(
                    prepared.plan, "duplicate_source_id:" + item.id);
            }
            if (item.uid.empty() || !uids.insert(item.uid).second) {
                add_blocker(
                    prepared.plan,
                    item.uid.empty()
                        ? "missing_source_uid:" + item.id
                        : "duplicate_source_uid:" + item.uid);
            }
            prepared.plan.identities.push_back(
                ProductRelocationIdentity{
                    item.id,
                    item.uid,
                    bounded_product_ref(
                        prepared.plan.product,
                        relative.generic_string()),
                    manifest->sha256,
                });
            prepared.parsed_identities.push_back(
                {std::move(item), relative.generic_string(),
                 manifest->sha256});
        } catch (const std::exception&) {
            add_blocker(
                prepared.plan,
                "canonical_item_unreadable:" +
                    bounded_product_ref(
                        prepared.plan.product,
                        relative.generic_string()));
        }
    }
}

void scan_registry_identities(
    PreparedRelocation& prepared,
    const ProjectConfig& project
) {
    std::unordered_set<std::string> source_ids;
    std::unordered_set<std::string> source_uids;
    std::unordered_set<std::string> global_ids;
    std::unordered_set<std::string> global_uids;
    std::unordered_map<std::string, std::string> id_owners;
    std::unordered_map<std::string, std::string> uid_owners;
    StreamingSha256 registry_revision;
    for (const auto& identity : prepared.plan.identities) {
        source_ids.insert(identity.id);
        source_uids.insert(identity.uid);
        global_ids.insert(identity.id);
        global_uids.insert(identity.uid);
        id_owners.emplace(identity.id, prepared.plan.product);
        uid_owners.emplace(identity.uid, prepared.plan.product);
    }
    std::size_t scanned = prepared.plan.identities.size();
    for (const auto& [name, definition] : project.products) {
        (void)definition;
        if (name == prepared.plan.product) {
            continue;
        }
        const ProductResolution canonical_resolution{
            name,
            ProductResolutionKind::CanonicalSlug,
            name,
            ProjectConfig::normalize_product_selector(name),
            name,
        };
        const auto root =
            project.resolve_backlog_root(
                canonical_resolution, prepared.config_path);
        if (!root) {
            add_blocker(
                prepared.plan,
                "collision_scan_root_unresolved:" + name);
            continue;
        }
        const auto normalized = normalized_absolute(*root);
        if (normalized == prepared.destination_root) {
            add_blocker(
                prepared.plan,
                "destination_registered_to_other_product:" + name);
            continue;
        }
        if (!std::filesystem::exists(normalized)) {
            add_blocker(
                prepared.plan,
                "collision_scan_root_missing:" + name);
            continue;
        }
        CanonicalStore store(normalized);
        for (const auto& path : store.list_items()) {
            if (++scanned > prepared.plan.request.max_items) {
                add_blocker(
                    prepared.plan,
                    "registry_item_scan_limit_exceeded");
                return;
            }
            try {
                const auto item = store.read_metadata(path);
                const auto id_inserted = global_ids.insert(item.id).second;
                const auto uid_inserted =
                    item.uid.empty() ||
                    global_uids.insert(item.uid).second;
                registry_revision.update(name);
                registry_revision.update("\0", 1u);
                registry_revision.update(item.id);
                registry_revision.update("\0", 1u);
                registry_revision.update(item.uid);
                registry_revision.update("\n", 1u);
                if (source_ids.contains(item.id)) {
                    add_blocker(
                        prepared.plan,
                        "display_id_collision:" + item.id + ":" + name);
                } else if (!id_inserted) {
                    add_blocker(
                        prepared.plan,
                        "registry_display_id_collision:" + item.id +
                            ":" + id_owners[item.id] + ":" + name);
                }
                if (!item.uid.empty() &&
                    source_uids.contains(item.uid)) {
                    add_blocker(
                        prepared.plan,
                        "uid_collision:" + item.uid + ":" + name);
                } else if (!item.uid.empty() && !uid_inserted) {
                    add_blocker(
                        prepared.plan,
                        "registry_uid_collision:" + item.uid + ":" +
                            uid_owners[item.uid] + ":" + name);
                }
                id_owners.try_emplace(item.id, name);
                if (!item.uid.empty()) {
                    uid_owners.try_emplace(item.uid, name);
                }
            } catch (const std::exception&) {
                add_blocker(
                    prepared.plan,
                    "collision_scan_item_unreadable:" + name);
            }
        }
    }

    std::size_t refs_checked = 0;
    std::size_t refs_resolved = 0;
    std::size_t refs_missing = 0;
    for (const auto& parsed : prepared.parsed_identities) {
        for (const auto& ref : structured_refs(parsed.item)) {
            ++refs_checked;
            if (looks_like_raw_path(ref)) {
                add_blocker(
                    prepared.plan,
                    "raw_filesystem_navigation_ref:" +
                        parsed.item.id);
                continue;
            }
            if (!looks_like_item_id(ref)) {
                continue;
            }
            if (global_ids.contains(ref)) {
                ++refs_resolved;
            } else {
                ++refs_missing;
                if (refs_missing <= 1000u) {
                    prepared.plan.warnings.push_back(
                        "missing_reference:" + parsed.item.id + ":" +
                        ref);
                }
            }
        }
    }
    prepared.plan.reference_checks.push_back(
        "structured_refs_checked:" + std::to_string(refs_checked));
    prepared.plan.reference_checks.push_back(
        "structured_refs_resolved:" + std::to_string(refs_resolved));
    prepared.plan.reference_checks.push_back(
        "structured_refs_missing:" + std::to_string(refs_missing));
    prepared.plan.reference_checks.push_back(
        "registry_identity_revision:" +
        registry_revision.final_hex());
}

PreparedRelocation build_prepared(
    const kano::backlog_ops::ProductRelocationOps::PlanOptions& options
) {
    PreparedRelocation prepared;
    prepared.plan.request = options.request;
    prepared.plan.validation_steps = {
        "canonical_byte_manifest_matches",
        "config_commit_resolves_shared_root",
        "derived_metadata_index_rebuilt_from_canonical",
        "display_ids_and_uids_match",
        "historical_worklog_receipt_and_artifact_bytes_match",
        "sequence_and_reservation_state_matches",
        "structured_reference_diagnostics_match",
    };

    if (options.request.product.empty()) {
        add_blocker(prepared.plan, "product_required");
    }
    if (options.request.max_files == 0 ||
        options.request.max_files > 1000000u) {
        add_blocker(prepared.plan, "invalid_max_files");
    }
    if (options.request.max_bytes == 0 ||
        options.request.max_bytes >
            256ull * 1024ull * 1024ull * 1024ull) {
        add_blocker(prepared.plan, "invalid_max_bytes");
    }
    if (options.request.max_items == 0 ||
        options.request.max_items > 1000000u) {
        add_blocker(prepared.plan, "invalid_max_items");
    }

    if (options.backlog_root) {
        prepared.config_root =
            normalized_absolute(*options.backlog_root);
        prepared.config_path =
            prepared.config_root / ".kano" / "backlog_config.toml";
    } else {
        const auto config = ConfigLoader::find_project_config(
            normalized_absolute(options.start_path));
        if (config) {
            prepared.config_path = normalized_absolute(*config);
            const auto root =
                ConfigLoader::resolve_project_root(prepared.config_path);
            if (root) {
                prepared.config_root = normalized_absolute(*root);
            }
        }
    }
    if (prepared.config_root.empty() ||
        !std::filesystem::is_regular_file(prepared.config_path)) {
        add_blocker(prepared.plan, "shared_backlog_config_not_found");
        finalize_plan(prepared);
        return prepared;
    }
    prepared.plan.config_ref =
        "project-config:.kano/backlog_config.toml";
    try {
        prepared.config_before = read_file(prepared.config_path);
        prepared.plan.config_revision =
            sha256_hex(prepared.config_before);
    } catch (const std::exception& error) {
        add_blocker(
            prepared.plan,
            "shared_backlog_config_unreadable:" +
                std::string(error.what()));
        finalize_plan(prepared);
        return prepared;
    }
    if (std::filesystem::exists(
            std::filesystem::path(
                prepared.config_path.string() +
                ".kob-product-relocation.tmp")) ||
        std::filesystem::exists(
            std::filesystem::path(
                prepared.config_path.string() + ".lock"))) {
        add_blocker(prepared.plan, "config_dirty_or_locked");
    }

    const auto project =
        ProjectConfig::load_from_toml(prepared.config_path);
    if (!project) {
        add_blocker(prepared.plan, "shared_backlog_config_invalid");
        finalize_plan(prepared);
        return prepared;
    }
    for (const auto& collision :
         project->find_prefix_collisions(prepared.config_path)) {
        add_blocker(
            prepared.plan,
            "existing_prefix_collision:" + collision.prefix + ":" +
                collision.left_product + ":" +
                collision.right_product);
    }
    const auto product_resolution =
        project->resolve_product(options.request.product);
    if (!product_resolution) {
        add_blocker(
            prepared.plan,
            "product_not_registered:" + options.request.product);
        finalize_plan(prepared);
        return prepared;
    }
    const auto& product_name = product_resolution->canonical_slug;
    prepared.plan.product = product_name;
    const auto definition = project->get_product(product_name);
    const auto source_root =
        project->resolve_backlog_root(
            *product_resolution, prepared.config_path);
    if (!definition || !source_root) {
        add_blocker(
            prepared.plan, "product_config_incomplete:" + product_name);
        finalize_plan(prepared);
        return prepared;
    }
    prepared.plan.prefix = definition->prefix;
    prepared.source_root = normalized_absolute(*source_root);
    prepared.destination_root =
        options.request.destination_root.empty()
            ? normalized_absolute(
                  prepared.config_root / "products" / product_name)
            : normalized_absolute(options.request.destination_root);
    const auto expected_destination = normalized_absolute(
        prepared.config_root / "products" / product_name);
    prepared.plan.source_root_ref =
        "product:" + product_name + ":configured-root";
    prepared.plan.destination_root_ref =
        "product:" + product_name + ":shared-root";
    prepared.plan.destination_path_digest =
        sha256_hex(prepared.destination_root.generic_string());

    if (prepared.destination_root != expected_destination) {
        add_blocker(
            prepared.plan,
            "destination_not_canonical_shared_product_root");
    }
    if (prepared.source_root == prepared.destination_root) {
        add_blocker(prepared.plan, "product_already_uses_shared_root");
    }
    if (is_within_or_equal(
            prepared.destination_root, prepared.source_root) ||
        is_within_or_equal(
            prepared.source_root, prepared.destination_root)) {
        add_blocker(prepared.plan, "source_destination_nested");
    }
    if (!std::filesystem::is_directory(prepared.source_root) ||
        std::filesystem::is_symlink(prepared.source_root)) {
        add_blocker(
            prepared.plan, "configured_source_root_not_relocatable");
    }
    if (std::filesystem::exists(prepared.destination_root)) {
        if (!std::filesystem::is_directory(
                prepared.destination_root) ||
            std::filesystem::is_symlink(prepared.destination_root)) {
            add_blocker(
                prepared.plan, "destination_not_empty_directory");
        } else {
            std::error_code empty_error;
            const bool empty =
                std::filesystem::is_empty(
                    prepared.destination_root, empty_error);
            if (empty_error || !empty) {
                add_blocker(
                    prepared.plan, "destination_not_empty_directory");
            } else {
                prepared.plan.destination_preexisted_empty = true;
            }
        }
    }

    try {
        prepared.config_after = rewrite_product_root(
            prepared.config_before, product_name,
            "products/" + product_name);
    } catch (const std::exception& error) {
        add_blocker(prepared.plan, error.what());
    }

    if (std::filesystem::is_directory(prepared.source_root)) {
        inventory_source(prepared);
        collect_source_identities(prepared);
        try {
            prepared.sequence_state =
                read_sequence_state(prepared.source_root, prepared.plan);
        } catch (const std::exception& error) {
            add_blocker(prepared.plan, error.what());
        }
        prepared.plan.sequence_state_revision =
            prepared.sequence_state.revision;
        prepared.plan.sequence_count =
            prepared.sequence_state.sequences.size();
        prepared.plan.reservation_count =
            prepared.sequence_state.reservations.size();
        scan_registry_identities(prepared, *project);
    }
    if (options.request.expected_source_revision &&
        *options.request.expected_source_revision !=
            prepared.plan.source_revision) {
        add_blocker(prepared.plan, "expected_source_revision_mismatch");
    }
    prepared.plan.derived_surfaces.push_back(
        "target:product-cache:" + product_name +
        "/index/backlog.db:rebuild");
    prepared.plan.warnings.push_back(
        "derived_views_are_not_copied_as_authority");
    prepared.plan.warnings.push_back(
        "source_root_is_retained_as_verified_rollback_material");
    finalize_plan(prepared);
    return prepared;
}

Json::Value manifest_json(const std::vector<ManifestEntry>& manifest) {
    Json::Value values(Json::arrayValue);
    for (const auto& file : manifest) {
        Json::Value value(Json::objectValue);
        value["relative"] = file.relative;
        value["kind"] = file.kind;
        value["size"] = static_cast<Json::UInt64>(file.size);
        value["sha256"] = file.sha256;
        values.append(value);
    }
    return values;
}

std::vector<ManifestEntry> manifest_from_json(
    const Json::Value& values
) {
    std::vector<ManifestEntry> result;
    std::set<std::string> seen;
    for (const auto& value : values) {
        const auto relative = value["relative"].asString();
        const auto path = std::filesystem::path(relative);
        if (relative.empty() || path.is_absolute() ||
            first_component(path) == ".." ||
            path.lexically_normal().generic_string() != relative ||
            !seen.insert(relative).second ||
            !is_sha256(value["sha256"].asString()) ||
            value["size"].asUInt64() > kMaximumSingleFileBytes) {
            throw std::runtime_error(
                "invalid_relocation_manifest");
        }
        result.push_back({
            relative,
            value["kind"].asString(),
            value["size"].asUInt64(),
            value["sha256"].asString(),
        });
    }
    std::sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.relative < right.relative;
        });
    return result;
}

std::filesystem::path resolve_config_root(
    const kano::backlog_ops::ProductRelocationOps::RecoveryOptions& options
) {
    if (options.backlog_root) {
        return normalized_absolute(*options.backlog_root);
    }
    const auto config = ConfigLoader::find_project_config(
        normalized_absolute(options.start_path));
    if (!config) {
        throw std::runtime_error("shared_backlog_config_not_found");
    }
    const auto root = ConfigLoader::resolve_project_root(*config);
    if (!root) {
        throw std::runtime_error("shared_backlog_root_not_found");
    }
    return normalized_absolute(*root);
}

std::filesystem::path transaction_root(
    const std::filesystem::path& config_root,
    const std::string& plan_hash
) {
    if (!is_sha256(plan_hash)) {
        throw std::runtime_error("invalid_relocation_plan_hash");
    }
    return config_root / ".kano" / "cache" /
           "product-relocations" / plan_hash;
}

std::string receipt_ref(const std::string& plan_hash) {
    return "project-cache:product-relocations/" + plan_hash +
           "/journal.json";
}

Json::Value load_journal(const std::filesystem::path& transaction) {
    const auto path = transaction / "journal.json";
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("relocation_journal_not_found");
    }
    const auto journal = parse_json(read_file(path));
    if (journal["schema"].asString() != kJournalSchema) {
        throw std::runtime_error("unsupported_relocation_journal_schema");
    }
    return journal;
}

void write_journal(
    const std::filesystem::path& transaction,
    const Json::Value& journal
) {
    write_file_atomic(
        transaction / "journal.json", json_string(journal, true));
}

class DirectoryLock {
public:
    DirectoryLock(
        std::filesystem::path path,
        std::string plan_hash
    ) : path_(std::move(path)), plan_hash_(std::move(plan_hash)) {
        std::filesystem::create_directories(path_.parent_path());
        std::error_code error;
        if (!std::filesystem::create_directory(path_, error) || error) {
            if (!reclaim_stale_lock()) {
                throw std::runtime_error("relocation_lock_active");
            }
            error.clear();
            if (!std::filesystem::create_directory(path_, error) ||
                error) {
                throw std::runtime_error("relocation_lock_active");
            }
        }
        try {
            Json::Value owner(Json::objectValue);
            owner["schema"] =
                "kob.product_root_relocation.lock.v1";
            owner["plan_hash"] = plan_hash_;
            owner["pid"] = static_cast<Json::UInt64>(
                current_process_id());
            owner["created_at"] = current_utc_timestamp();
            write_file_atomic(
                path_ / "owner.json", json_string(owner, true));
        } catch (...) {
            std::filesystem::remove_all(path_, error);
            throw;
        }
    }

    ~DirectoryLock() {
        std::error_code error;
        std::filesystem::remove(path_ / "owner.json", error);
        error.clear();
        std::filesystem::remove(path_, error);
    }

    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;

private:
    static std::uint64_t current_process_id() {
#ifdef _WIN32
        return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        return static_cast<std::uint64_t>(getpid());
#endif
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

    bool reclaim_stale_lock() const {
        const auto owner_path = path_ / "owner.json";
        if (!std::filesystem::is_regular_file(owner_path)) {
            return false;
        }
        try {
            const auto owner = parse_json(read_file(owner_path));
            if (owner["schema"].asString() !=
                    "kob.product_root_relocation.lock.v1" ||
                process_is_alive(owner["pid"].asUInt64())) {
                return false;
            }
        } catch (const std::exception&) {
            return false;
        }
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        return !error;
    }

    std::filesystem::path path_;
    std::string plan_hash_;
};

std::filesystem::path stage_root(
    const std::filesystem::path& destination,
    const std::string& plan_hash
) {
    const auto short_hash = plan_hash.substr(0, 16);
    return destination.parent_path() /
           (destination.filename().generic_string() +
            ".kob-relocation-" + short_hash + ".stage");
}

std::filesystem::path retired_root(
    const std::filesystem::path& source,
    const std::string& plan_hash
) {
    const auto short_hash = plan_hash.substr(0, 16);
    return source.parent_path() /
           (source.filename().generic_string() +
            ".kob-relocation-" + short_hash + ".retired");
}

bool managed_paths_valid(
    const Json::Value& journal,
    const std::filesystem::path& config_root,
    const std::string& plan_hash,
    const std::filesystem::path& transaction
) {
    const auto product = journal["product"].asString();
    const auto config = normalized_absolute(
        std::filesystem::path(journal["config_path"].asString()));
    const auto source = normalized_absolute(
        std::filesystem::path(journal["source_root"].asString()));
    const auto destination = normalized_absolute(
        std::filesystem::path(journal["destination_root"].asString()));
    const auto stage = normalized_absolute(
        std::filesystem::path(journal["stage_root"].asString()));
    const auto retired = normalized_absolute(
        std::filesystem::path(journal["retired_root"].asString()));
    const auto config_before_path = transaction / "config.before";
    if (!std::filesystem::is_regular_file(config_before_path)) {
        return false;
    }
    const auto config_before = read_file(config_before_path);
    if (sha256_hex(config_before) !=
            journal["config_before_sha256"].asString() ||
        journal["plan"]["config_revision"].asString() !=
            journal["config_before_sha256"].asString()) {
        return false;
    }
    const auto before_project =
        ProjectConfig::load_from_toml(config_before_path);
    const ProductResolution canonical_resolution{
        product,
        ProductResolutionKind::CanonicalSlug,
        product,
        ProjectConfig::normalize_product_selector(product),
        product,
    };
    const auto resolved_source =
        before_project
            ? before_project->resolve_backlog_root(
                  canonical_resolution, config)
            : std::nullopt;
    if (!resolved_source) {
        return false;
    }
    std::string expected_after;
    try {
        expected_after = rewrite_product_root(
            config_before, product, "products/" + product);
    } catch (const std::exception&) {
        return false;
    }
    return !product.empty() &&
           config == config_root / ".kano" / "backlog_config.toml" &&
           destination == config_root / "products" / product &&
           source == normalized_absolute(*resolved_source) &&
           source != destination &&
           stage == stage_root(destination, plan_hash) &&
           retired == retired_root(source, plan_hash) &&
           stage.parent_path() == destination.parent_path() &&
           retired.parent_path() == source.parent_path() &&
           sha256_hex(expected_after) ==
               journal["config_after_sha256"].asString();
}

std::vector<std::string> verify_manifest(
    const std::filesystem::path& root,
    const std::vector<ManifestEntry>& manifest,
    const std::string& product,
    const std::string& label
) {
    std::vector<std::string> failures;
    if (!std::filesystem::is_directory(root) ||
        std::filesystem::is_symlink(root)) {
        failures.push_back(label + "_root_missing");
        return failures;
    }
    std::set<std::string> expected;
    for (const auto& file : manifest) {
        expected.insert(file.relative);
        const auto path = root / file.relative;
        try {
            const auto digest = digest_file(path);
            if (digest.size != file.size ||
                digest.sha256 != file.sha256) {
                failures.push_back(
                    label + "_hash_mismatch:" +
                    bounded_product_ref(product, file.relative));
            }
        } catch (const std::exception&) {
            failures.push_back(
                label + "_file_missing:" +
                bounded_product_ref(product, file.relative));
        }
    }

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::none, iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    if (iterator_error) {
        failures.push_back(label + "_inventory_failed");
        return failures;
    }
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            failures.push_back(label + "_inventory_failed");
            break;
        }
        const auto& entry = *iterator;
        if (entry.is_symlink()) {
            failures.push_back(label + "_symlink_detected");
            if (entry.is_directory()) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto relative = entry.path().lexically_relative(root);
        if (is_derived_relative(relative)) {
            continue;
        }
        const auto generic = relative.generic_string();
        if (!expected.contains(generic)) {
            failures.push_back(
                label + "_unexpected_file:" +
                bounded_product_ref(product, generic));
        }
    }
    sort_unique(failures);
    return failures;
}

void copy_manifest(
    const PreparedRelocation& prepared,
    const std::filesystem::path& stage
) {
    if (std::filesystem::exists(stage)) {
        throw std::runtime_error("stage_root_already_exists");
    }
    std::filesystem::create_directories(stage);
    for (const auto& file : prepared.manifest) {
        const auto source = prepared.source_root / file.relative;
        const auto destination = stage / file.relative;
        const auto source_digest = digest_file(source);
        if (source_digest.size != file.size ||
            source_digest.sha256 != file.sha256) {
            throw std::runtime_error(
                "concurrent_source_drift:" +
                bounded_product_ref(
                    prepared.plan.product, file.relative));
        }
        std::filesystem::create_directories(destination.parent_path());
        std::error_code error;
        std::filesystem::copy_file(
            source, destination,
            std::filesystem::copy_options::none, error);
        if (error) {
            throw std::runtime_error(
                "stage_copy_failed:" +
                bounded_product_ref(
                    prepared.plan.product, file.relative));
        }
        const auto destination_digest = digest_file(destination);
        if (destination_digest.size != file.size ||
            destination_digest.sha256 != file.sha256) {
            throw std::runtime_error(
                "stage_copy_hash_mismatch:" +
                bounded_product_ref(
                    prepared.plan.product, file.relative));
        }
    }
    const auto failures = verify_manifest(
        stage, prepared.manifest, prepared.plan.product, "stage");
    if (!failures.empty()) {
        throw std::runtime_error(failures.front());
    }
}

bool config_resolves_destination(
    const std::filesystem::path& config_path,
    const std::string& product,
    const std::filesystem::path& destination
) {
    const auto project = ProjectConfig::load_from_toml(config_path);
    if (!project ||
        !project->find_prefix_collisions(config_path).empty()) {
        return false;
    }
    const ProductResolution canonical_resolution{
        product,
        ProductResolutionKind::CanonicalSlug,
        product,
        ProjectConfig::normalize_product_selector(product),
        product,
    };
    const auto root =
        project->resolve_backlog_root(
            canonical_resolution, config_path);
    return root &&
           normalized_absolute(*root) ==
               normalized_absolute(destination);
}

Json::Value make_journal(
    const PreparedRelocation& prepared,
    const std::filesystem::path& stage,
    const std::filesystem::path& retired
) {
    Json::Value journal(Json::objectValue);
    journal["schema"] = kJournalSchema;
    journal["status"] = "prepared";
    journal["stage"] = "journal";
    journal["plan_hash"] = prepared.plan.plan_hash;
    journal["product"] = prepared.plan.product;
    journal["prefix"] = prepared.plan.prefix;
    journal["created_at"] = current_utc_timestamp();
    journal["config_path"] = prepared.config_path.generic_string();
    journal["source_root"] = prepared.source_root.generic_string();
    journal["destination_root"] =
        prepared.destination_root.generic_string();
    journal["stage_root"] = stage.generic_string();
    journal["retired_root"] = retired.generic_string();
    journal["destination_preexisted_empty"] =
        prepared.plan.destination_preexisted_empty;
    journal["config_before_sha256"] =
        sha256_hex(prepared.config_before);
    journal["config_after_sha256"] =
        sha256_hex(prepared.config_after);
    journal["manifest"] = manifest_json(prepared.manifest);
    journal["sequence_state"] =
        sequence_state_json(prepared.sequence_state);
    journal["plan"] = plan_json(prepared.plan, true);
    return journal;
}

bool remove_exact_tree(
    const std::filesystem::path& path,
    const std::filesystem::path& expected_parent,
    const std::string& expected_name,
    std::vector<std::string>& failures,
    const std::string& failure_code
) {
    if (path.parent_path() != expected_parent ||
        path.filename().generic_string() != expected_name) {
        failures.push_back(failure_code + "_path_guard_failed");
        return false;
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        failures.push_back(failure_code + "_remove_failed");
        return false;
    }
    return true;
}

std::vector<std::string> rollback_journal(
    const std::filesystem::path& config_root,
    const std::filesystem::path& transaction,
    Json::Value& journal,
    std::vector<std::string>& failures
) {
    std::vector<std::string> restored;
    const auto plan_hash = journal["plan_hash"].asString();
    if (!managed_paths_valid(
            journal, config_root, plan_hash, transaction)) {
        failures.push_back("journal_managed_path_guard_failed");
        return restored;
    }
    const auto product = journal["product"].asString();
    const auto config = normalized_absolute(
        std::filesystem::path(journal["config_path"].asString()));
    const auto source = normalized_absolute(
        std::filesystem::path(journal["source_root"].asString()));
    const auto destination = normalized_absolute(
        std::filesystem::path(journal["destination_root"].asString()));
    const auto stage = normalized_absolute(
        std::filesystem::path(journal["stage_root"].asString()));
    const auto retired = normalized_absolute(
        std::filesystem::path(journal["retired_root"].asString()));
    const auto manifest = manifest_from_json(journal["manifest"]);

    const bool source_exists = std::filesystem::exists(source);
    const bool retired_exists = std::filesystem::exists(retired);
    if (source_exists && retired_exists) {
        failures.push_back("source_and_retired_roots_both_exist");
        return restored;
    }
    if (source_exists) {
        const auto source_failures =
            verify_manifest(source, manifest, product, "source");
        failures.insert(
            failures.end(),
            source_failures.begin(), source_failures.end());
    } else if (retired_exists) {
        const auto retired_failures =
            verify_manifest(retired, manifest, product, "retired");
        if (!retired_failures.empty()) {
            failures.insert(
                failures.end(),
                retired_failures.begin(), retired_failures.end());
            return restored;
        }
        std::error_code error;
        std::filesystem::rename(retired, source, error);
        if (error) {
            failures.push_back("source_restore_rename_failed");
            return restored;
        }
        restored.push_back(
            "product:" + product + ":configured-root");
    } else {
        failures.push_back("source_and_retired_roots_missing");
        return restored;
    }
    if (!failures.empty()) {
        return restored;
    }

    const auto current_config = read_file(config);
    const auto current_hash = sha256_hex(current_config);
    const auto before_hash =
        journal["config_before_sha256"].asString();
    const auto after_hash =
        journal["config_after_sha256"].asString();
    if (current_hash == after_hash) {
        const auto before = read_file(transaction / "config.before");
        if (sha256_hex(before) != before_hash) {
            failures.push_back("config_backup_hash_mismatch");
            return restored;
        }
        write_file_atomic(config, before);
        restored.push_back("project-config:.kano/backlog_config.toml");
    } else if (current_hash != before_hash) {
        failures.push_back("config_rollback_drift");
        return restored;
    }

    if (std::filesystem::exists(destination)) {
        std::error_code empty_error;
        const bool untouched_preexisting_empty =
            journal["destination_preexisted_empty"].asBool() &&
            (journal["stage"].asString() == "journal" ||
             journal["stage"].asString() == "stage_verified") &&
            std::filesystem::is_directory(destination) &&
            std::filesystem::is_empty(destination, empty_error) &&
            !empty_error;
        if (!untouched_preexisting_empty) {
            const auto target_failures =
                verify_manifest(destination, manifest, product, "target");
            if (!target_failures.empty()) {
                failures.insert(
                    failures.end(),
                    target_failures.begin(), target_failures.end());
                return restored;
            }
            if (!remove_exact_tree(
                    destination, config_root / "products", product,
                    failures, "target")) {
                return restored;
            }
            restored.push_back(
                "product:" + product + ":shared-root-removed");
        }
    }
    if (journal["destination_preexisted_empty"].asBool() &&
        !std::filesystem::exists(destination)) {
        std::filesystem::create_directories(destination);
        restored.push_back(
            "product:" + product + ":shared-empty-root-restored");
    }

    if (std::filesystem::exists(stage)) {
        const auto expected_name =
            destination.filename().generic_string() +
            ".kob-relocation-" + plan_hash.substr(0, 16) + ".stage";
        remove_exact_tree(
            stage, destination.parent_path(), expected_name,
            failures, "stage");
    }
    sort_unique(restored);
    sort_unique(failures);
    return restored;
}

void publish_stage(
    const PreparedRelocation& prepared,
    const std::filesystem::path& stage
) {
    if (std::filesystem::exists(prepared.destination_root)) {
        std::error_code empty_error;
        if (!std::filesystem::is_directory(
                prepared.destination_root) ||
            !std::filesystem::is_empty(
                prepared.destination_root, empty_error) ||
            empty_error) {
            throw std::runtime_error(
                "destination_changed_since_plan");
        }
        std::error_code remove_error;
        if (!std::filesystem::remove(
                prepared.destination_root, remove_error) ||
            remove_error) {
            throw std::runtime_error(
                "destination_empty_root_retire_failed");
        }
    }
    std::error_code error;
    std::filesystem::rename(
        stage, prepared.destination_root, error);
    if (error) {
        throw std::runtime_error("target_publish_failed");
    }
}

void rebuild_target_index(
    const PreparedRelocation& prepared
) {
    const auto index =
        prepared.destination_root / ".cache" / "index" / "backlog.db";
    kano::backlog_ops::build_index(
        prepared.destination_root, index, true, prepared.plan.product);
    if (prepared.sequence_state.source_database_exists) {
        restore_sequence_state(index, prepared.sequence_state);
    } else {
        kano::backlog_ops::BacklogIndex sequence_index(
            index, prepared.plan.product, prepared.destination_root);
        sequence_index.sync_sequences(prepared.destination_root);
    }
}

} // namespace

namespace kano::backlog_ops {

bool ProductRelocationPlan::ready() const {
    return status == "ready" && blockers.empty();
}

std::string ProductRelocationPlan::to_json(bool pretty) const {
    return json_string(plan_json(*this, true), pretty);
}

std::string ProductRelocationResult::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["changed_refs"] = string_array(changed_refs);
    value["operation_receipts"] = string_array(operation_receipts);
    value["receipt_ref"] = receipt_ref;
    value["recovery_status"] = recovery_status;
    value["idempotent_replay"] = idempotent_replay;
    return json_string(value, pretty);
}

std::string ProductRelocationVerification::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["postconditions"] = string_array(postconditions);
    value["failures"] = string_array(failures);
    return json_string(value, pretty);
}

std::string ProductRelocationStatus::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["stage"] = stage;
    value["recovery_status"] = recovery_status;
    value["rollback_supported"] = rollback_supported;
    return json_string(value, pretty);
}

std::string ProductRelocationRollback::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["restored_refs"] = string_array(restored_refs);
    value["failures"] = string_array(failures);
    return json_string(value, pretty);
}

ProductRelocationPlan ProductRelocationOps::plan(
    const PlanOptions& options
) {
    return build_prepared(options).plan;
}

ProductRelocationResult ProductRelocationOps::apply(
    const ApplyOptions& options
) {
    ProductRelocationResult result;
    result.status = "blocked";
    result.plan_hash = options.expected_plan_hash;
    result.recovery_status = "none";
    if (!options.confirm) {
        result.operation_receipts.push_back("confirmation_required");
        return result;
    }
    if (!is_sha256(options.expected_plan_hash)) {
        result.operation_receipts.push_back(
            "invalid_relocation_plan_hash");
        return result;
    }

    RecoveryOptions recovery_locator;
    recovery_locator.start_path = options.plan.start_path;
    recovery_locator.backlog_root = options.plan.backlog_root;
    recovery_locator.plan_hash = options.expected_plan_hash;
    std::filesystem::path config_root;
    std::filesystem::path transaction;
    try {
        config_root = resolve_config_root(recovery_locator);
        transaction =
            transaction_root(config_root, options.expected_plan_hash);
        if (std::filesystem::is_regular_file(
                transaction / "journal.json")) {
            const auto existing = load_journal(transaction);
            const auto existing_status =
                existing["status"].asString();
            if (existing_status == "applied") {
                const auto verification = verify(recovery_locator);
                if (verification.status == "verified") {
                    result.status = "applied";
                    result.recovery_status = "available";
                    result.receipt_ref =
                        receipt_ref(options.expected_plan_hash);
                    result.idempotent_replay = true;
                    result.operation_receipts = {
                        "idempotent_replay",
                        "postconditions_verified",
                    };
                    return result;
                }
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts =
                    verification.failures;
                return result;
            }
            result.recovery_status =
                existing_status == "rolled_back"
                    ? "completed"
                    : "required";
            result.operation_receipts.push_back(
                "existing_transaction_requires_recovery:" +
                existing_status);
            return result;
        }
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(
            bounded_error(error.what()));
        return result;
    }

    const auto prepared = build_prepared(options.plan);
    result.plan_hash = prepared.plan.plan_hash;
    if (!prepared.plan.ready()) {
        result.operation_receipts = prepared.plan.blockers;
        return result;
    }
    if (prepared.plan.plan_hash != options.expected_plan_hash) {
        result.plan_hash = options.expected_plan_hash;
        result.operation_receipts.push_back(
            "stale_or_mismatched_plan_hash");
        return result;
    }

    const auto stage =
        stage_root(prepared.destination_root, prepared.plan.plan_hash);
    const auto retired =
        retired_root(prepared.source_root, prepared.plan.plan_hash);
    Json::Value journal;
    try {
        DirectoryLock lock(
            prepared.config_root / ".kano" / "cache" /
            "product-relocations" / "apply.lock",
            prepared.plan.plan_hash);
        if (std::filesystem::exists(stage)) {
            throw std::runtime_error("stage_root_already_exists");
        }
        if (std::filesystem::exists(retired)) {
            throw std::runtime_error("retired_root_already_exists");
        }
        if (std::filesystem::exists(transaction)) {
            throw std::runtime_error(
                "transaction_root_already_exists");
        }
        if (sha256_hex(read_file(prepared.config_path)) !=
            prepared.plan.config_revision) {
            throw std::runtime_error(
                "concurrent_config_drift_before_stage");
        }

        std::filesystem::create_directories(transaction);
        write_file_atomic(
            transaction / "config.before",
            prepared.config_before);
        journal = make_journal(prepared, stage, retired);
        write_journal(transaction, journal);

        const auto mark_stage =
            [&](const std::string& phase,
                const std::string& status = "applying") {
                journal["stage"] = phase;
                journal["status"] = status;
                write_journal(transaction, journal);
            };
        const auto inject_failure =
            [&](const std::string& phase) {
                if (options.inject_failure_after &&
                    *options.inject_failure_after == phase) {
                    throw std::runtime_error(
                        "injected_failure:" + phase);
                }
            };
        const auto inject_interruption =
            [&](const std::string& phase) {
                if (!options.inject_interruption_after ||
                    *options.inject_interruption_after != phase) {
                    return false;
                }
                journal["stage"] = phase;
                journal["status"] = "recovery_required";
                journal["last_error"] =
                    "injected_interruption:" + phase;
                write_journal(transaction, journal);
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.receipt_ref =
                    receipt_ref(prepared.plan.plan_hash);
                result.operation_receipts.push_back(
                    "recoverable_interruption:" + phase);
                return true;
            };

        copy_manifest(prepared, stage);
        mark_stage("stage_verified");
        inject_failure("after_stage");
        if (inject_interruption("after_stage")) {
            return result;
        }

        publish_stage(prepared, stage);
        mark_stage("target_published");
        inject_failure("after_target_publish");
        if (inject_interruption("after_target_publish")) {
            return result;
        }

        if (sha256_hex(read_file(prepared.config_path)) !=
            prepared.plan.config_revision) {
            throw std::runtime_error(
                "concurrent_config_drift_before_commit");
        }
        write_file_atomic(
            prepared.config_path, prepared.config_after);
        if (!config_resolves_destination(
                prepared.config_path, prepared.plan.product,
                prepared.destination_root)) {
            throw std::runtime_error(
                "config_commit_verification_failed");
        }
        mark_stage("config_published");
        inject_failure("after_config_publish");
        if (inject_interruption("after_config_publish")) {
            return result;
        }

        rebuild_target_index(prepared);
        const auto target_failures = verify_manifest(
            prepared.destination_root, prepared.manifest,
            prepared.plan.product, "target");
        if (!target_failures.empty()) {
            throw std::runtime_error(target_failures.front());
        }
        const auto source_failures = verify_manifest(
            prepared.source_root, prepared.manifest,
            prepared.plan.product, "source");
        if (!source_failures.empty()) {
            throw std::runtime_error(source_failures.front());
        }
        mark_stage("target_verified");

        std::error_code retire_error;
        std::filesystem::rename(
            prepared.source_root, retired, retire_error);
        if (retire_error) {
            throw std::runtime_error("source_retire_failed");
        }
        mark_stage("source_retired");
        inject_failure("after_source_retire");
        if (inject_interruption("after_source_retire")) {
            return result;
        }

        journal["stage"] = "complete";
        journal["status"] = "applied";
        journal["applied_at"] = current_utc_timestamp();
        write_journal(transaction, journal);

        RecoveryOptions recovery;
        recovery.backlog_root = prepared.config_root;
        recovery.plan_hash = prepared.plan.plan_hash;
        const auto verification = verify(recovery);
        if (verification.status != "verified") {
            throw std::runtime_error(
                verification.failures.empty()
                    ? "postcondition_verification_failed"
                    : verification.failures.front());
        }

        result.status = "applied";
        result.recovery_status = "available";
        result.receipt_ref =
            receipt_ref(prepared.plan.plan_hash);
        result.changed_refs = {
            prepared.plan.config_ref,
            prepared.plan.destination_root_ref,
            "product:" + prepared.plan.product +
                ":retired-rollback-root",
            "product-cache:" + prepared.plan.product +
                "/index/backlog.db",
        };
        result.operation_receipts = {
            "reviewed_plan_hash_matched",
            "canonical_bytes_staged_and_verified",
            "target_published_on_destination_volume",
            "config_commit_published_atomically",
            "product_index_rebuilt_from_canonical",
            "sequence_and_reservation_state_preserved",
            "source_retained_as_verified_rollback_material",
            "postconditions_verified",
        };
        sort_unique(result.changed_refs);
        sort_unique(result.operation_receipts);
        return result;
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(
            bounded_error(error.what()));
        if (!transaction.empty() &&
            std::filesystem::is_regular_file(
                transaction / "journal.json")) {
            try {
                journal = load_journal(transaction);
                std::vector<std::string> failures;
                result.changed_refs = rollback_journal(
                    config_root, transaction, journal, failures);
                if (failures.empty()) {
                    journal["status"] = "rolled_back";
                    journal["stage"] = "automatic_rollback";
                    journal["last_error"] = error.what();
                    write_journal(transaction, journal);
                    result.status = "rolled_back";
                    result.recovery_status = "completed";
                    result.operation_receipts.push_back(
                        "automatic_rollback_completed");
                } else {
                    journal["status"] = "recovery_required";
                    journal["stage"] = "automatic_rollback_failed";
                    journal["last_error"] = error.what();
                    write_journal(transaction, journal);
                    result.status = "recovery_required";
                    result.recovery_status = "required";
                    result.operation_receipts.insert(
                        result.operation_receipts.end(),
                        failures.begin(), failures.end());
                }
            } catch (const std::exception& recovery_error) {
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts.push_back(
                    "automatic_rollback_failed:" +
                    bounded_error(recovery_error.what()));
            }
        }
        sort_unique(result.changed_refs);
        sort_unique(result.operation_receipts);
        return result;
    }
}

ProductRelocationVerification ProductRelocationOps::verify(
    const RecoveryOptions& options
) {
    ProductRelocationVerification verification;
    verification.status = "not_applied";
    verification.plan_hash = options.plan_hash;
    try {
        const auto config_root = resolve_config_root(options);
        const auto transaction =
            transaction_root(config_root, options.plan_hash);
        const auto journal = load_journal(transaction);
        if (journal["plan_hash"].asString() != options.plan_hash) {
            verification.failures.push_back(
                "journal_plan_hash_mismatch");
        }
        if (!managed_paths_valid(
                journal, config_root, options.plan_hash,
                transaction)) {
            verification.failures.push_back(
                "journal_managed_path_guard_failed");
        }
        if (journal["status"].asString() != "applied") {
            verification.failures.push_back(
                "relocation_not_applied:" +
                journal["status"].asString());
        }
        auto embedded_plan = journal["plan"];
        embedded_plan["plan_hash"] = "";
        if (sha256_hex(json_string(embedded_plan, false)) !=
            options.plan_hash) {
            verification.failures.push_back(
                "embedded_plan_hash_mismatch");
        }

        const auto product = journal["product"].asString();
        const auto config = normalized_absolute(
            std::filesystem::path(
                journal["config_path"].asString()));
        const auto source = normalized_absolute(
            std::filesystem::path(
                journal["source_root"].asString()));
        const auto destination = normalized_absolute(
            std::filesystem::path(
                journal["destination_root"].asString()));
        const auto retired = normalized_absolute(
            std::filesystem::path(
                journal["retired_root"].asString()));
        const auto manifest =
            manifest_from_json(journal["manifest"]);

        if (!std::filesystem::is_regular_file(config) ||
            sha256_hex(read_file(config)) !=
                journal["config_after_sha256"].asString()) {
            verification.failures.push_back(
                "config_commit_hash_mismatch");
        } else if (!config_resolves_destination(
                       config, product, destination)) {
            verification.failures.push_back(
                "config_does_not_resolve_shared_root");
        } else {
            verification.postconditions.push_back(
                "config_commit_resolves_shared_root");
        }
        if (std::filesystem::exists(source)) {
            verification.failures.push_back(
                "active_source_root_still_exists");
        }

        const auto target_failures = verify_manifest(
            destination, manifest, product, "target");
        verification.failures.insert(
            verification.failures.end(),
            target_failures.begin(), target_failures.end());
        if (target_failures.empty()) {
            verification.postconditions.push_back(
                "canonical_target_manifest_matches");
        }
        const auto retired_failures = verify_manifest(
            retired, manifest, product, "retired");
        verification.failures.insert(
            verification.failures.end(),
            retired_failures.begin(), retired_failures.end());
        if (retired_failures.empty()) {
            verification.postconditions.push_back(
                "retired_source_manifest_matches");
        }

        CanonicalStore target_store(destination);
        const auto identity_prefix =
            "product:" + product + "/";
        for (const auto& identity :
             embedded_plan["identities"]) {
            const auto ref = identity["source_ref"].asString();
            if (!ref.starts_with(identity_prefix)) {
                verification.failures.push_back(
                    "identity_ref_not_bounded");
                continue;
            }
            const auto relative =
                ref.substr(identity_prefix.size());
            try {
                const auto path = destination / relative;
                const auto item = target_store.read(path);
                const auto digest = digest_file(path);
                if (item.id != identity["id"].asString() ||
                    item.uid != identity["uid"].asString() ||
                    digest.sha256 !=
                        identity["source_sha256"].asString()) {
                    verification.failures.push_back(
                        "item_identity_mismatch:" +
                        identity["id"].asString());
                }
            } catch (const std::exception&) {
                verification.failures.push_back(
                    "target_item_unreadable:" +
                    identity["id"].asString());
            }
        }
        if (std::none_of(
                verification.failures.begin(),
                verification.failures.end(),
                [](const auto& failure) {
                    return failure.starts_with(
                               "item_identity_mismatch:") ||
                           failure.starts_with(
                               "target_item_unreadable:") ||
                           failure == "identity_ref_not_bounded";
                })) {
            verification.postconditions.push_back(
                "display_ids_uids_and_item_bytes_match");
        }

        const auto index =
            destination / ".cache" / "index" / "backlog.db";
        if (!std::filesystem::is_regular_file(index)) {
            verification.failures.push_back(
                "derived_metadata_index_missing");
        } else {
            const auto doctor = doctor_metadata_index(
                index, destination, product, true);
            if (!doctor.healthy) {
                verification.failures.push_back(
                    "derived_metadata_index_unhealthy");
            } else {
                verification.postconditions.push_back(
                    "derived_metadata_index_rebuilt_and_healthy");
            }
        }

        const auto expected_sequence =
            sequence_state_from_json(journal["sequence_state"]);
        if (expected_sequence.source_database_exists) {
            ProductRelocationPlan diagnostics;
            diagnostics.product = product;
            const auto actual_sequence =
                read_sequence_state(destination, diagnostics);
            if (!diagnostics.blockers.empty() ||
                !sequence_states_equal(
                    expected_sequence, actual_sequence)) {
                verification.failures.push_back(
                    "sequence_or_reservation_state_mismatch");
            } else {
                verification.postconditions.push_back(
                    "sequence_and_reservation_state_matches");
            }
        } else if (std::filesystem::is_regular_file(index)) {
            verification.postconditions.push_back(
                "sequence_state_derived_from_canonical");
        }

        if (target_failures.empty() &&
            retired_failures.empty()) {
            verification.postconditions.push_back(
                "worklog_receipt_artifact_and_evidence_bytes_match");
            verification.postconditions.push_back(
                "structured_refs_preserved_byte_exact");
        }
        sort_unique(verification.postconditions);
        sort_unique(verification.failures);
        verification.status =
            verification.failures.empty() ? "verified" : "failed";
    } catch (const std::exception& error) {
        verification.failures.push_back(
            bounded_error(error.what()));
        verification.status =
            std::string(error.what()) ==
                    "relocation_journal_not_found"
                ? "not_applied"
                : "failed";
        sort_unique(verification.failures);
    }
    return verification;
}

ProductRelocationStatus ProductRelocationOps::status(
    const RecoveryOptions& options
) {
    ProductRelocationStatus result;
    result.status = "not_applied";
    result.plan_hash = options.plan_hash;
    result.recovery_status = "none";
    try {
        const auto config_root = resolve_config_root(options);
        const auto journal = load_journal(
            transaction_root(config_root, options.plan_hash));
        result.status = journal["status"].asString();
        result.stage = journal["stage"].asString();
        if (result.status == "applied") {
            result.recovery_status = "available";
            result.rollback_supported = true;
        } else if (
            result.status == "prepared" ||
            result.status == "applying" ||
            result.status == "recovery_required") {
            result.recovery_status = "required";
            result.rollback_supported = true;
        } else if (result.status == "rolled_back") {
            result.recovery_status = "completed";
        }
    } catch (const std::exception& error) {
        if (std::string(error.what()) !=
            "relocation_journal_not_found") {
            result.status = "failed";
            result.recovery_status =
                bounded_error(error.what());
        }
    }
    return result;
}

ProductRelocationRollback ProductRelocationOps::rollback(
    const RecoveryOptions& options
) {
    ProductRelocationRollback result;
    result.status = "blocked";
    result.plan_hash = options.plan_hash;
    if (!options.confirm) {
        result.failures.push_back("confirmation_required");
        return result;
    }
    try {
        const auto config_root = resolve_config_root(options);
        const auto transaction =
            transaction_root(config_root, options.plan_hash);
        auto journal = load_journal(transaction);
        if (journal["plan_hash"].asString() != options.plan_hash) {
            result.failures.push_back(
                "journal_plan_hash_mismatch");
            return result;
        }
        auto embedded_plan = journal["plan"];
        embedded_plan["plan_hash"] = "";
        if (sha256_hex(json_string(embedded_plan, false)) !=
            options.plan_hash) {
            result.failures.push_back(
                "embedded_plan_hash_mismatch");
            return result;
        }
        DirectoryLock lock(
            config_root / ".kano" / "cache" /
                "product-relocations" / "apply.lock",
            options.plan_hash);
        if (journal["status"].asString() == "rolled_back") {
            result.status = "rolled_back";
            return result;
        }
        result.restored_refs = rollback_journal(
            config_root, transaction, journal, result.failures);
        if (result.failures.empty()) {
            journal["status"] = "rolled_back";
            journal["stage"] = "rollback_complete";
            journal["rolled_back_at"] = current_utc_timestamp();
            write_journal(transaction, journal);
            result.status = "rolled_back";
        } else {
            journal["status"] = "recovery_required";
            journal["stage"] = "rollback_failed";
            write_journal(transaction, journal);
            result.status = "recovery_required";
        }
    } catch (const std::exception& error) {
        result.failures.push_back(
            bounded_error(error.what()));
        result.status = "failed";
    }
    sort_unique(result.restored_refs);
    sort_unique(result.failures);
    return result;
}

} // namespace kano::backlog_ops
