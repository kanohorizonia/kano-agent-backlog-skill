#include "kano/backlog_ops/prefix_migration/prefix_migration_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"

#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {

using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ConfigLoader;
using kano::backlog_core::ProjectConfig;
using kano::backlog_core::ProductResolution;
using kano::backlog_core::ProductResolutionKind;
using kano::backlog_ops::BacklogIndex;
using kano::backlog_ops::PrefixMigrationFileChange;
using kano::backlog_ops::PrefixMigrationItemMapping;
using kano::backlog_ops::PrefixMigrationPlan;
using kano::backlog_ops::PrefixMigrationRequest;

constexpr std::uintmax_t kMaximumSingleFileBytes = 256ull * 1024ull * 1024ull;
constexpr const char* kReceiptSchemaV2 = "kob.product_prefix_migration.receipt.v2";
constexpr const char* kReceiptSchemaV3 = "kob.product_prefix_migration.receipt.v3";
constexpr const char* kJournalSchemaV2 = "kob.product_prefix_migration.journal.v2";
constexpr const char* kJournalSchemaV3 = "kob.product_prefix_migration.journal.v3";

ProductResolution trusted_canonical_product_resolution(
    const std::string& canonical_slug
) {
    return ProductResolution{
        canonical_slug,
        ProductResolutionKind::CanonicalSlug,
        canonical_slug,
        ProjectConfig::normalize_product_selector(canonical_slug),
        canonical_slug,
    };
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool is_valid_agent(const std::string& agent) {
    static const std::regex grammar(R"(^[A-Za-z][A-Za-z0-9._-]{0,63}$)");
    static const std::set<std::string> placeholders = {
        "agent", "anonymous", "assistant", "auto", "na", "none", "null",
        "placeholder", "todo", "unknown", "unset", "user",
    };
    return std::regex_match(agent, grammar) &&
           !placeholders.contains(lowercase_ascii(agent));
}

std::string require_mutation_agent(const std::optional<std::string>& agent) {
    if (!agent) {
        throw std::runtime_error("agent_required");
    }
    if (!is_valid_agent(*agent)) {
        throw std::runtime_error("invalid_agent");
    }
    return *agent;
}

void reject_read_only_agent(const std::optional<std::string>& agent) {
    if (agent) {
        throw std::runtime_error("agent_not_allowed_for_read_only_mode");
    }
}

bool read_valid_agent_field(
    const Json::Value& value,
    const char* field,
    std::optional<std::string>& agent
) {
    agent.reset();
    if (!value.isMember(field) || value[field].isNull()) {
        return true;
    }
    if (!value[field].isString() || !is_valid_agent(value[field].asString())) {
        return false;
    }
    agent = value[field].asString();
    return true;
}

Json::Value nullable_string(const std::optional<std::string>& value) {
    return value ? Json::Value(*value) : Json::Value(Json::nullValue);
}

std::filesystem::path normalized_absolute(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        normalized = std::filesystem::absolute(path, ec);
    }
    if (ec) {
        throw std::runtime_error("Unable to normalize path: " + path.generic_string());
    }
    return normalized.lexically_normal();
}

bool is_within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto relative = child.lexically_relative(parent);
    return !relative.empty() && !relative.is_absolute() && relative.generic_string().find("..") != 0;
}

std::string relative_path(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto normalized_path = normalized_absolute(path);
    const auto normalized_root = normalized_absolute(root);
    if (normalized_path == normalized_root) {
        return ".";
    }
    if (!is_within(normalized_path, normalized_root)) {
        throw std::runtime_error("Prefix migration path escapes backlog root: " + path.generic_string());
    }
    return normalized_path.lexically_relative(normalized_root).generic_string();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to read prefix migration input: " + path.generic_string());
    }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > kMaximumSingleFileBytes) {
        throw std::runtime_error("Prefix migration file exceeds the single-file bound: " + path.generic_string());
    }
    input.seekg(0, std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file_atomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".kob-prefix-migration.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("Unable to stage prefix migration output: " + temporary.generic_string());
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output.good()) {
            throw std::runtime_error("Unable to write prefix migration output: " + temporary.generic_string());
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Unable to publish prefix migration output: " + path.generic_string() + ":" + ec.message());
    }
}

uint32_t sha256_rotr(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

class StreamingSha256 {
public:
    void update(const char* data, std::size_t size) {
        constexpr auto kMaximumBytes = std::numeric_limits<std::uint64_t>::max() / 8u;
        if (size > kMaximumBytes - total_bytes_) {
            throw std::runtime_error("sha256_input_too_large");
        }
        total_bytes_ += static_cast<std::uint64_t>(size);
        while (size > 0) {
            const auto take = std::min(size, buffer_.size() - buffer_size_);
            std::copy_n(
                reinterpret_cast<const std::uint8_t*>(data),
                take,
                buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
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
            std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(), 0u);
            transform(buffer_.data());
            buffer_size_ = 0;
        }
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.begin() + 56, 0u);
        for (std::size_t index = 0; index < 8u; ++index) {
            buffer_[56u + index] = static_cast<std::uint8_t>(bit_length >> (56u - index * 8u));
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
        static constexpr std::array<uint32_t, 64> kRoundConstants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774u, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        std::array<uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16u; ++index) {
            const std::size_t base = index * 4u;
            words[index] = (static_cast<uint32_t>(block[base]) << 24u) |
                           (static_cast<uint32_t>(block[base + 1u]) << 16u) |
                           (static_cast<uint32_t>(block[base + 2u]) << 8u) |
                           static_cast<uint32_t>(block[base + 3u]);
        }
        for (std::size_t index = 16u; index < 64u; ++index) {
            const uint32_t s0 = sha256_rotr(words[index - 15u], 7u) ^ sha256_rotr(words[index - 15u], 18u) ^ (words[index - 15u] >> 3u);
            const uint32_t s1 = sha256_rotr(words[index - 2u], 17u) ^ sha256_rotr(words[index - 2u], 19u) ^ (words[index - 2u] >> 10u);
            words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
        }
        uint32_t a = hash_[0], b = hash_[1], c = hash_[2], d = hash_[3];
        uint32_t e = hash_[4], f = hash_[5], g = hash_[6], h = hash_[7];
        for (std::size_t index = 0; index < 64u; ++index) {
            const uint32_t s1 = sha256_rotr(e, 6u) ^ sha256_rotr(e, 11u) ^ sha256_rotr(e, 25u);
            const uint32_t choice = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + choice + kRoundConstants[index] + words[index];
            const uint32_t s0 = sha256_rotr(a, 2u) ^ sha256_rotr(a, 13u) ^ sha256_rotr(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash_[0] += a; hash_[1] += b; hash_[2] += c; hash_[3] += d;
        hash_[4] += e; hash_[5] += f; hash_[6] += g; hash_[7] += h;
    }

    std::array<std::uint32_t, 8> hash_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
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
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
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
        const auto line_end = newline == std::string::npos ? rendered.size() : newline;
        auto content_end = line_end;
        while (content_end > line_start &&
               (rendered[content_end - 1] == ' ' || rendered[content_end - 1] == '\t')) {
            --content_end;
        }
        normalized.append(rendered, line_start, content_end - line_start);
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
        throw std::runtime_error("Invalid prefix migration JSON: " + errors);
    }
    return value;
}

template <typename T>
void sort_unique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::size_t worklog_offset(const std::string& text) {
    if (text.starts_with("# Worklog")) {
        return 0;
    }
    const auto marker = text.find("\n# Worklog");
    return marker == std::string::npos ? text.size() : marker + 1u;
}

bool is_id_boundary(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return !(std::isalnum(value) || ch == '-');
}

std::string replace_id_token(const std::string& text, const std::string& old_id, const std::string& new_id) {
    std::string output;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto position = text.find(old_id, cursor);
        if (position == std::string::npos) {
            output.append(text.substr(cursor));
            break;
        }
        const auto after = position + old_id.size();
        const bool left_ok = position == 0 || is_id_boundary(text[position - 1u]);
        const bool right_ok = after == text.size() || is_id_boundary(text[after]);
        if (left_ok && right_ok) {
            output.append(text.substr(cursor, position - cursor));
            output.append(new_id);
            cursor = after;
        } else {
            output.append(text.substr(cursor, after - cursor));
            cursor = after;
        }
    }
    return output;
}

std::string rewrite_product_ids(
    const std::string& text,
    const std::map<std::string, std::string>& mappings,
    const std::string& from_prefix,
    const std::string& to_prefix,
    bool preserve_worklog
) {
    const auto boundary = preserve_worklog ? worklog_offset(text) : text.size();
    std::string head = text.substr(0, boundary);
    static const std::regex id_regex(
        "[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4}");
    std::set<std::string> source_ids;
    for (std::sregex_iterator it(head.begin(), head.end(), id_regex), end;
         it != end; ++it) {
        if (const auto id = it->str(); id.starts_with(from_prefix + "-")) {
            source_ids.insert(id);
        }
    }
    for (const auto& source_id : source_ids) {
        const auto mapping = mappings.find(source_id);
        const auto target_id = mapping == mappings.end()
            ? to_prefix + source_id.substr(from_prefix.size())
            : mapping->second;
        head = replace_id_token(head, source_id, target_id);
    }
    return head + text.substr(boundary);
}

std::set<std::string> item_ids_before_worklog(const std::string& text, const std::string& prefix) {
    static const std::regex id_regex(
        "[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4}");
    const auto head = text.substr(0, worklog_offset(text));
    std::set<std::string> result;
    for (std::sregex_iterator it(head.begin(), head.end(), id_regex), end; it != end; ++it) {
        const auto id = it->str();
        if (id.starts_with(prefix + "-")) {
            result.insert(id);
        }
    }
    return result;
}

std::string rewrite_config_prefix(
    const std::string& content,
    const std::string& product,
    const std::string& old_prefix,
    const std::string& new_prefix
) {
    const std::string section = "[products." + product + "]";
    const auto section_start = content.find(section);
    if (section_start == std::string::npos) {
        throw std::runtime_error("product_config_section_not_found:" + product);
    }
    const auto section_end = content.find("\n[", section_start + section.size());
    const auto bounded_end = section_end == std::string::npos ? content.size() : section_end;
    const std::string old_line = "prefix = \"" + old_prefix + "\"";
    const auto prefix_pos = content.find(old_line, section_start + section.size());
    if (prefix_pos == std::string::npos || prefix_pos >= bounded_end) {
        throw std::runtime_error("product_config_prefix_not_found:" + product + ":" + old_prefix);
    }
    auto updated = content;
    updated.replace(prefix_pos, old_line.size(), "prefix = \"" + new_prefix + "\"");
    return updated;
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

Json::Value string_array(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) {
        result.append(value);
    }
    return result;
}

Json::Value request_json(const PrefixMigrationRequest& request) {
    Json::Value value(Json::objectValue);
    value["product"] = request.product;
    value["expected_from_prefix"] = request.expected_from_prefix.value_or("");
    value["to_prefix"] = request.to_prefix;
    value["max_files"] = static_cast<Json::UInt64>(request.max_files);
    value["max_bytes"] = static_cast<Json::UInt64>(request.max_bytes);
    return value;
}

Json::Value plan_json(const PrefixMigrationPlan& plan, bool include_hash) {
    Json::Value value(Json::objectValue);
    value["schema"] = plan.schema;
    value["status"] = plan.status;
    value["request"] = request_json(plan.request);
    value["product"] = plan.product;
    value["from_prefix"] = plan.from_prefix;
    value["to_prefix"] = plan.to_prefix;
    value["source_revision"] = plan.source_revision;
    value["config_path"] = plan.config_path;
    value["compatibility_policy"] = plan.compatibility_policy;
    Json::Value items(Json::arrayValue);
    for (const auto& item : plan.items) {
        Json::Value entry(Json::objectValue);
        entry["source_id"] = item.source_id;
        entry["target_id"] = item.target_id;
        entry["uid"] = item.uid;
        entry["source_path"] = item.source_path;
        entry["target_path"] = item.target_path;
        items.append(entry);
    }
    value["items"] = items;
    Json::Value files(Json::arrayValue);
    for (const auto& file : plan.files) {
        Json::Value entry(Json::objectValue);
        entry["action"] = file.action;
        entry["kind"] = file.kind;
        entry["source_path"] = file.source_path;
        entry["target_path"] = file.target_path;
        entry["rewrites_canonical_refs"] = file.rewrites_canonical_refs;
        entry["preserves_file_bytes"] = file.preserves_file_bytes;
        files.append(entry);
    }
    value["files"] = files;
    value["resolver_checks"] = string_array(plan.resolver_checks);
    value["preserved_historical_surfaces"] = string_array(plan.preserved_historical_surfaces);
    value["required_external_updates"] = string_array(plan.required_external_updates);
    value["blockers"] = string_array(plan.blockers);
    value["warnings"] = string_array(plan.warnings);
    value["plan_hash"] = include_hash ? plan.plan_hash : "";
    value["dry_run"] = plan.dry_run;
    value["mutates_backlog"] = plan.mutates_backlog;
    return value;
}

struct FileMutation {
    std::string path;
    std::string kind;
    bool before_exists = false;
    std::string before_content;
    std::optional<std::string> after_content;
};

struct PreparedPrefixMigration {
    PrefixMigrationPlan plan;
    std::filesystem::path backlog_root;
    std::filesystem::path config_path;
    std::filesystem::path product_root;
    std::map<std::string, std::string> id_mappings;
    std::vector<FileMutation> operations;
};

void add_blocker(PrefixMigrationPlan& plan, const std::string& blocker) {
    plan.blockers.push_back(blocker);
    plan.status = "blocked";
}

void add_file_change(
    PrefixMigrationPlan& plan,
    const std::string& action,
    const std::string& kind,
    const std::string& source_path,
    const std::string& target_path,
    bool rewrites_refs,
    bool preserves_bytes
) {
    plan.files.push_back(PrefixMigrationFileChange{
        action, kind, source_path, target_path, rewrites_refs, preserves_bytes});
}

void add_mutation(
    PreparedPrefixMigration& prepared,
    const std::string& path,
    const std::string& kind,
    std::optional<std::string> after_content
) {
    if (path.empty() || path == "." || std::filesystem::path(path).is_absolute() || path.find("..") == 0) {
        throw std::runtime_error("invalid_prefix_migration_operation_path:" + path);
    }
    const auto duplicate = std::find_if(
        prepared.operations.begin(), prepared.operations.end(),
        [&](const FileMutation& operation) { return operation.path == path; });
    if (duplicate != prepared.operations.end()) {
        throw std::runtime_error("duplicate_prefix_migration_operation:" + path);
    }
    const auto absolute = prepared.backlog_root / path;
    const bool exists = std::filesystem::is_regular_file(absolute);
    prepared.operations.push_back(FileMutation{
        path,
        kind,
        exists,
        exists ? read_file(absolute) : std::string{},
        std::move(after_content)});
}

void add_rename(
    PreparedPrefixMigration& prepared,
    const std::string& source_path,
    const std::string& target_path,
    const std::string& kind,
    const std::string& target_content
) {
    if (source_path == target_path) {
        add_mutation(prepared, source_path, kind, target_content);
        return;
    }
    if (std::filesystem::exists(prepared.backlog_root / target_path)) {
        add_blocker(prepared.plan, "destination_path_exists:" + target_path);
        return;
    }
    add_mutation(prepared, target_path, kind, target_content);
    add_mutation(prepared, source_path, kind + "_source", std::nullopt);
}

std::vector<std::filesystem::path> regular_files_under(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::is_directory(root)) {
        return result;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) {
            throw std::runtime_error("symlink_not_supported:" + entry.path().generic_string());
        }
        if (entry.is_regular_file()) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string replace_ids_in_path(
    std::string path,
    const std::map<std::string, std::string>& mappings
) {
    for (const auto& [source_id, target_id] : mappings) {
        path = replace_id_token(path, source_id, target_id);
    }
    return path;
}

std::string snapshot_revision(
    const std::set<std::string>& paths,
    const std::filesystem::path& root,
    const PrefixMigrationRequest& request,
    PrefixMigrationPlan& plan
) {
    if (paths.size() > request.max_files) {
        add_blocker(plan, "snapshot_file_limit_exceeded:" + std::to_string(paths.size()));
        return "";
    }
    std::uintmax_t total_bytes = 0;
    StreamingSha256 snapshot;
    for (const auto& path : paths) {
        const auto absolute = root / path;
        const auto content = read_file(absolute);
        if (content.size() > request.max_bytes - total_bytes) {
            add_blocker(plan, "snapshot_byte_limit_exceeded");
            return "";
        }
        total_bytes += content.size();
        snapshot.update(path);
        snapshot.update("\0", 1u);
        snapshot.update(sha256_hex(content));
        snapshot.update("\n", 1u);
    }
    return snapshot.final_hex();
}

std::string receipt_relative_path(const PreparedPrefixMigration& prepared) {
    return relative_path(
        prepared.product_root / "_meta" / "prefix-migrations" /
            (prepared.plan.from_prefix + "-to-" + prepared.plan.to_prefix + ".json"),
        prepared.backlog_root);
}

std::filesystem::path transaction_root(
    const std::filesystem::path& backlog_root,
    const std::string& plan_hash
) {
    if (!is_sha256(plan_hash)) {
        throw std::runtime_error("invalid_prefix_migration_plan_hash");
    }
    return backlog_root / ".kano" / "cache" / "prefix-migrations" / plan_hash;
}

struct JournalOperation {
    std::string path;
    std::string kind;
    bool before_exists = false;
    std::string before_sha256;
    bool after_exists = false;
    std::string after_sha256;
    std::string backup_path;
    std::string stage_path;
};

Json::Value journal_operation_json(const JournalOperation& operation) {
    Json::Value value(Json::objectValue);
    value["path"] = operation.path;
    value["kind"] = operation.kind;
    value["before_exists"] = operation.before_exists;
    value["before_sha256"] = operation.before_sha256;
    value["after_exists"] = operation.after_exists;
    value["after_sha256"] = operation.after_sha256;
    value["backup_path"] = operation.backup_path;
    value["stage_path"] = operation.stage_path;
    return value;
}

std::filesystem::path canonical_journal_boundary(
    const std::filesystem::path& path,
    const char* error
) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        throw std::runtime_error(error);
    }
    return canonical.lexically_normal();
}

std::filesystem::path confined_journal_path(
    const std::filesystem::path& root,
    const std::string& raw_path,
    const char* error
) {
    const std::filesystem::path relative(raw_path);
    if (raw_path.empty() || relative.is_absolute() || relative.has_root_path()) {
        throw std::runtime_error(error);
    }
    const auto normalized = relative.lexically_normal();
    if (normalized.empty() || normalized == "." ||
        normalized.generic_string() != relative.generic_string()) {
        throw std::runtime_error(error);
    }
    const auto canonical_root = canonical_journal_boundary(root, error);
    const auto target =
        canonical_journal_boundary(canonical_root / normalized, error);
    if (!is_within(target, canonical_root)) {
        throw std::runtime_error(error);
    }
    return target;
}

JournalOperation journal_operation_from_json(
    const Json::Value& value,
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& transaction
) {
    if (!value.isObject() || !value["path"].isString() ||
        !value["kind"].isString() || !value["before_exists"].isBool() ||
        !value["before_sha256"].isString() ||
        !value["after_exists"].isBool() ||
        !value["after_sha256"].isString() ||
        !value["backup_path"].isString() ||
        !value["stage_path"].isString()) {
        throw std::runtime_error("invalid_journal_operation_shape");
    }
    JournalOperation operation;
    operation.path = value["path"].asString();
    operation.kind = value["kind"].asString();
    operation.before_exists = value["before_exists"].asBool();
    operation.before_sha256 = value["before_sha256"].asString();
    operation.after_exists = value["after_exists"].asBool();
    operation.after_sha256 = value["after_sha256"].asString();
    operation.backup_path = value["backup_path"].asString();
    operation.stage_path = value["stage_path"].asString();
    confined_journal_path(
        backlog_root, operation.path, "invalid_journal_operation_path");
    if (operation.kind.empty()) {
        throw std::runtime_error("invalid_journal_operation_kind");
    }
    if (operation.before_exists) {
        if (!is_sha256(operation.before_sha256)) {
            throw std::runtime_error("invalid_journal_before_hash");
        }
        const auto backup = confined_journal_path(
            transaction, operation.backup_path, "invalid_journal_backup_path");
        if (!is_within(
                backup,
                canonical_journal_boundary(
                    transaction / "backup", "invalid_journal_backup_path"))) {
            throw std::runtime_error("invalid_journal_backup_path");
        }
    } else if (!operation.before_sha256.empty() ||
               !operation.backup_path.empty()) {
        throw std::runtime_error("invalid_journal_backup_binding");
    }
    if (operation.after_exists) {
        if (!is_sha256(operation.after_sha256)) {
            throw std::runtime_error("invalid_journal_after_hash");
        }
        const auto stage = confined_journal_path(
            transaction, operation.stage_path, "invalid_journal_stage_path");
        if (!is_within(
                stage,
                canonical_journal_boundary(
                    transaction / "stage", "invalid_journal_stage_path"))) {
            throw std::runtime_error("invalid_journal_stage_path");
        }
    } else if (!operation.after_sha256.empty() || !operation.stage_path.empty()) {
        throw std::runtime_error("invalid_journal_stage_binding");
    }
    return operation;
}

std::vector<JournalOperation> journal_operations(
    const Json::Value& journal,
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& transaction
) {
    if (!journal["operations"].isArray()) {
        throw std::runtime_error("invalid_journal_operations");
    }
    std::vector<JournalOperation> result;
    std::set<std::string> paths;
    for (const auto& value : journal["operations"]) {
        auto operation =
            journal_operation_from_json(value, backlog_root, transaction);
        if (!paths.insert(operation.path).second) {
            throw std::runtime_error("duplicate_journal_operation_path");
        }
        result.push_back(std::move(operation));
    }
    return result;
}

Json::Value load_journal(const std::filesystem::path& transaction) {
    const auto path = transaction / "journal.json";
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("prefix_migration_journal_not_found");
    }
    const auto journal = parse_json(read_file(path));
    const auto schema = journal["schema"].asString();
    if (schema != kJournalSchemaV2 && schema != kJournalSchemaV3) {
        throw std::runtime_error("unsupported_prefix_migration_journal_schema");
    }
    return journal;
}

void write_journal(const std::filesystem::path& transaction, const Json::Value& journal) {
    write_file_atomic(transaction / "journal.json", json_string(journal, true));
}

bool file_matches(
    const std::filesystem::path& path,
    bool expected_exists,
    const std::string& expected_sha256
);

std::optional<std::string> optional_nonempty_string(
    const Json::Value& value,
    const char* field,
    const char* error
) {
    if (!value.isMember(field) || value[field].isNull()) {
        return std::nullopt;
    }
    if (!value[field].isString() || value[field].asString().empty()) {
        throw std::runtime_error(error);
    }
    return value[field].asString();
}

struct ValidatedJournalEvidence {
    Json::Value journal;
    Json::Value plan;
    std::vector<JournalOperation> operations;
    std::filesystem::path product_root;
    std::string status;
    std::string receipt_path;
    std::optional<std::string> apply_agent;
    std::optional<std::string> rollback_agent;
    std::optional<std::string> rollback_mode;
    std::optional<std::string> rollback_attempted_at;
    std::optional<std::string> rolled_back_at;
};

std::string bounded_recovery_error(const std::string& error) {
    constexpr std::size_t kMaximumRecoveryErrorBytes = 1024u;
    return error.size() <= kMaximumRecoveryErrorBytes
        ? error
        : error.substr(0, kMaximumRecoveryErrorBytes);
}

std::string merged_recovery_failures(
    const std::vector<std::string>& failures
) {
    auto ordered = failures;
    sort_unique(ordered);
    std::string merged;
    for (const auto& failure : ordered) {
        if (!merged.empty()) {
            merged += ";";
        }
        merged += failure;
    }
    return bounded_recovery_error(merged);
}

void validate_receipt_identity(
    const Json::Value& receipt,
    const Json::Value& plan,
    const std::string& requested_plan_hash,
    const std::string& journal_schema,
    const std::optional<std::string>& apply_agent,
    const char* identity_error,
    const char* actor_error
) {
    const auto expected_receipt_schema =
        journal_schema == kJournalSchemaV3 ? kReceiptSchemaV3 : kReceiptSchemaV2;
    if (receipt["schema"].asString() != expected_receipt_schema ||
        receipt["plan_hash"].asString() != requested_plan_hash ||
        receipt["product"] != plan["product"] ||
        receipt["from_prefix"] != plan["from_prefix"] ||
        receipt["to_prefix"] != plan["to_prefix"] ||
        receipt["source_revision"] != plan["source_revision"] ||
        receipt["compatibility_policy"] != plan["compatibility_policy"] ||
        receipt["preserved_historical_surfaces"] !=
            plan["preserved_historical_surfaces"] ||
        receipt["required_external_updates"] !=
            plan["required_external_updates"] ||
        receipt["item_mappings"] != plan["items"] ||
        receipt["transaction_status_ref"].asString() !=
            ".kano/cache/prefix-migrations/" + requested_plan_hash +
                "/journal.json" ||
        !receipt["timestamp"].isString() ||
        receipt["timestamp"].asString().empty()) {
        throw std::runtime_error(identity_error);
    }
    if (journal_schema == kJournalSchemaV3) {
        std::optional<std::string> receipt_agent;
        if (!read_valid_agent_field(receipt, "apply_agent", receipt_agent) ||
            receipt_agent != apply_agent) {
            throw std::runtime_error(actor_error);
        }
    }
}

const Json::Value* latest_rollback_attempt(const Json::Value& journal) {
    if (!journal.isMember("rollback_attempts")) {
        return nullptr;
    }
    const auto& attempts = journal["rollback_attempts"];
    if (!attempts.isArray() || attempts.empty()) {
        throw std::runtime_error("invalid_rollback_attempts");
    }
    for (Json::ArrayIndex index = 0; index < attempts.size(); ++index) {
        const auto& attempt = attempts[index];
        if (!attempt.isObject() || !attempt["agent"].isString() ||
            !is_valid_agent(attempt["agent"].asString()) ||
            !attempt["mode"].isString() ||
            (attempt["mode"].asString() != "manual" &&
             attempt["mode"].asString() != "automatic") ||
            !attempt["attempted_at"].isString() ||
            attempt["attempted_at"].asString().empty() ||
            !attempt["status"].isString()) {
            throw std::runtime_error("invalid_rollback_attempt");
        }
        const auto status = attempt["status"].asString();
        if (status == "in_progress") {
            if (attempt.isMember("failed_at") || attempt.isMember("completed_at") ||
                attempt.isMember("error")) {
                throw std::runtime_error("invalid_rollback_attempt");
            }
        } else if (status == "failed") {
            if (!attempt["failed_at"].isString() ||
                attempt["failed_at"].asString().empty() ||
                !attempt["error"].isString() ||
                attempt["error"].asString().empty() ||
                attempt["error"].asString().size() > 1024u ||
                attempt.isMember("completed_at")) {
                throw std::runtime_error("invalid_rollback_attempt");
            }
        } else if (status == "completed") {
            if (!attempt["completed_at"].isString() ||
                attempt["completed_at"].asString().empty() ||
                attempt.isMember("failed_at") || attempt.isMember("error")) {
                throw std::runtime_error("invalid_rollback_attempt");
            }
        } else {
            throw std::runtime_error("invalid_rollback_attempt");
        }
        if (index + 1u < attempts.size() && status != "failed") {
            throw std::runtime_error("invalid_rollback_attempt_history");
        }
    }
    return &attempts[attempts.size() - 1u];
}

std::size_t append_rollback_attempt(
    Json::Value& journal,
    const std::string& agent,
    const std::string& mode,
    const std::string& attempted_at
) {
    if (!journal.isMember("rollback_attempts")) {
        journal["rollback_attempts"] = Json::Value(Json::arrayValue);
    }
    if (!journal["rollback_attempts"].isArray()) {
        throw std::runtime_error("invalid_rollback_attempts");
    }
    auto& attempts = journal["rollback_attempts"];
    if (!attempts.empty()) {
        auto& latest = attempts[attempts.size() - 1u];
        const auto latest_status = latest["status"].asString();
        if (latest_status == "in_progress") {
            latest["status"] = "failed";
            latest["failed_at"] = current_utc_timestamp();
            latest["error"] = bounded_recovery_error(
                "interrupted_before_confirmed_retry");
        } else if (latest_status == "completed") {
            throw std::runtime_error(
                "completed_rollback_attempt_not_retryable");
        } else if (latest_status != "failed") {
            throw std::runtime_error("invalid_rollback_attempt");
        }
    }
    Json::Value attempt(Json::objectValue);
    attempt["agent"] = agent;
    attempt["mode"] = mode;
    attempt["attempted_at"] = attempted_at;
    attempt["status"] = "in_progress";
    attempts.append(attempt);
    journal["rollback_agent"] = agent;
    journal["rollback_mode"] = mode;
    journal["rollback_attempted_at"] = attempted_at;
    journal.removeMember("rolled_back_at");
    return attempts.size() - 1u;
}

void mark_rollback_attempt_failed(
    Json::Value& journal,
    std::size_t attempt_index,
    const std::string& error
) {
    auto& attempt = journal["rollback_attempts"][static_cast<Json::ArrayIndex>(attempt_index)];
    attempt["status"] = "failed";
    attempt.removeMember("completed_at");
    attempt["failed_at"] = current_utc_timestamp();
    attempt["error"] = bounded_recovery_error(error);
}

std::string mark_rollback_attempt_completed(
    Json::Value& journal,
    std::size_t attempt_index
) {
    const auto completed_at = current_utc_timestamp();
    auto& attempt = journal["rollback_attempts"][static_cast<Json::ArrayIndex>(attempt_index)];
    attempt["status"] = "completed";
    attempt.removeMember("failed_at");
    attempt.removeMember("error");
    attempt["completed_at"] = completed_at;
    journal["rolled_back_at"] = completed_at;
    return completed_at;
}

ValidatedJournalEvidence load_validated_journal(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& transaction,
    const std::string& requested_plan_hash
) {
    ValidatedJournalEvidence evidence;
    evidence.journal = load_journal(transaction);
    if (evidence.journal["plan_hash"].asString() != requested_plan_hash) {
        throw std::runtime_error("journal_plan_hash_mismatch");
    }
    if (!evidence.journal["plan"].isObject()) {
        throw std::runtime_error("invalid_embedded_plan");
    }
    evidence.plan = evidence.journal["plan"];
    if (evidence.plan["schema"].asString() !=
            kano::backlog_ops::kPrefixMigrationPlanSchema ||
        evidence.plan["plan_hash"].asString() != requested_plan_hash) {
        throw std::runtime_error("embedded_plan_identity_mismatch");
    }
    auto hash_input = evidence.plan;
    hash_input["plan_hash"] = "";
    if (sha256_hex(json_string(hash_input, false)) != requested_plan_hash) {
        throw std::runtime_error("embedded_plan_hash_mismatch");
    }
    if (evidence.plan["status"].asString() != "ready" ||
        !evidence.plan["product"].isString() ||
        evidence.plan["product"].asString().empty() ||
        !evidence.plan["from_prefix"].isString() ||
        evidence.plan["from_prefix"].asString().empty() ||
        !evidence.plan["to_prefix"].isString() ||
        evidence.plan["to_prefix"].asString().empty() ||
        !evidence.plan["source_revision"].isString() ||
        !is_sha256(evidence.plan["source_revision"].asString()) ||
        !evidence.plan["config_path"].isString()) {
        throw std::runtime_error("invalid_embedded_plan_identity");
    }

    evidence.operations =
        journal_operations(evidence.journal, backlog_root, transaction);
    if (!evidence.journal["status"].isString()) {
        throw std::runtime_error("invalid_journal_status");
    }
    evidence.status = evidence.journal["status"].asString();
    if (evidence.status != "prepared" && evidence.status != "applying" &&
        evidence.status != "applied" &&
        evidence.status != "recovery_required" &&
        evidence.status != "rolled_back") {
        throw std::runtime_error("invalid_journal_status");
    }
    if (!evidence.journal["receipt_path"].isString()) {
        throw std::runtime_error("invalid_journal_receipt_path");
    }
    evidence.receipt_path = evidence.journal["receipt_path"].asString();
    const auto receipt_absolute = confined_journal_path(
        backlog_root, evidence.receipt_path, "invalid_journal_receipt_path");

    const auto config_path = confined_journal_path(
        backlog_root, evidence.plan["config_path"].asString(),
        "invalid_embedded_plan_config_path");
    const auto project = ProjectConfig::load_from_toml(config_path);
    if (!project) {
        throw std::runtime_error("embedded_plan_config_unreadable");
    }
    const auto product_resolution = trusted_canonical_product_resolution(
        evidence.plan["product"].asString());
    const auto product_root =
        project->resolve_backlog_root(product_resolution, config_path);
    if (!product_root) {
        throw std::runtime_error("embedded_plan_product_root_missing");
    }
    const auto normalized_backlog_root = normalized_absolute(backlog_root);
    evidence.product_root = normalized_absolute(*product_root);
    if (!is_within(evidence.product_root, normalized_backlog_root)) {
        throw std::runtime_error("embedded_plan_product_root_outside_backlog");
    }
    const auto expected_receipt = relative_path(
        evidence.product_root / "_meta" / "prefix-migrations" /
            (evidence.plan["from_prefix"].asString() + "-to-" +
             evidence.plan["to_prefix"].asString() + ".json"),
        normalized_backlog_root);
    if (evidence.receipt_path != expected_receipt) {
        throw std::runtime_error("journal_receipt_path_mismatch");
    }

    const JournalOperation* receipt_operation = nullptr;
    for (const auto& operation : evidence.operations) {
        if (operation.kind != "migration_receipt") {
            continue;
        }
        if (receipt_operation != nullptr) {
            throw std::runtime_error("duplicate_journal_receipt_operation");
        }
        receipt_operation = &operation;
    }
    if (receipt_operation == nullptr ||
        receipt_operation->path != evidence.receipt_path ||
        receipt_operation->before_exists || !receipt_operation->after_exists) {
        throw std::runtime_error("journal_receipt_operation_mismatch");
    }

    const auto schema = evidence.journal["schema"].asString();
    if (!read_valid_agent_field(
            evidence.journal, "rollback_agent", evidence.rollback_agent)) {
        throw std::runtime_error("invalid_journal_rollback_agent");
    }
    evidence.rollback_mode = optional_nonempty_string(
        evidence.journal, "rollback_mode", "invalid_journal_rollback_mode");
    evidence.rollback_attempted_at = optional_nonempty_string(
        evidence.journal, "rollback_attempted_at",
        "invalid_journal_rollback_timestamp");
    evidence.rolled_back_at = optional_nonempty_string(
        evidence.journal, "rolled_back_at",
        "invalid_journal_rollback_timestamp");
    if (evidence.rollback_mode && *evidence.rollback_mode != "manual" &&
        *evidence.rollback_mode != "automatic") {
        throw std::runtime_error("invalid_journal_rollback_mode");
    }
    if (schema == kJournalSchemaV3) {
        if (!read_valid_agent_field(
                evidence.journal, "apply_agent", evidence.apply_agent) ||
            !evidence.apply_agent) {
            throw std::runtime_error("invalid_journal_apply_agent");
        }
    }

    const auto* latest_attempt = latest_rollback_attempt(evidence.journal);
    if (latest_attempt != nullptr) {
        if (!evidence.rollback_agent || !evidence.rollback_mode ||
            !evidence.rollback_attempted_at ||
            *evidence.rollback_agent != (*latest_attempt)["agent"].asString() ||
            *evidence.rollback_mode != (*latest_attempt)["mode"].asString() ||
            *evidence.rollback_attempted_at !=
                (*latest_attempt)["attempted_at"].asString()) {
            throw std::runtime_error("rollback_attempt_top_level_mismatch");
        }
        const auto attempt_status = (*latest_attempt)["status"].asString();
        if (evidence.status == "recovery_required") {
            if ((attempt_status != "in_progress" && attempt_status != "failed") ||
                evidence.rolled_back_at) {
                throw std::runtime_error("rollback_attempt_state_mismatch");
            }
        } else if (evidence.status == "rolled_back") {
            if (attempt_status != "completed" || !evidence.rolled_back_at ||
                *evidence.rolled_back_at !=
                    (*latest_attempt)["completed_at"].asString()) {
                throw std::runtime_error("rollback_attempt_state_mismatch");
            }
        } else {
            throw std::runtime_error("rollback_attempt_state_mismatch");
        }
        if (*evidence.rollback_mode == "automatic" &&
            schema == kJournalSchemaV3 &&
            evidence.rollback_agent != evidence.apply_agent) {
            throw std::runtime_error("automatic_rollback_agent_mismatch");
        }
    } else if (schema == kJournalSchemaV3) {
        const bool has_attempt = evidence.rollback_agent.has_value() ||
                                 evidence.rollback_mode.has_value() ||
                                 evidence.rollback_attempted_at.has_value();
        if (evidence.status == "recovery_required" ||
            evidence.status == "rolled_back") {
            throw std::runtime_error("incomplete_v3_rollback_provenance");
        }
        if (has_attempt || evidence.rolled_back_at) {
            throw std::runtime_error("unexpected_v3_rollback_provenance");
        }
    }
    if (schema == kJournalSchemaV3 && latest_attempt != nullptr) {
        for (const auto& attempt : evidence.journal["rollback_attempts"]) {
            if (attempt["mode"].asString() == "automatic" &&
                attempt["agent"].asString() != *evidence.apply_agent) {
                throw std::runtime_error("automatic_rollback_agent_mismatch");
            }
        }
    }

    const bool receipt_exists = std::filesystem::is_regular_file(receipt_absolute);
    if (evidence.status == "applied" && !receipt_exists) {
        throw std::runtime_error("migration_receipt_missing");
    }
    const auto validate_canonical_receipt = [&]() {
        if (!file_matches(
                receipt_absolute, receipt_operation->after_exists,
                receipt_operation->after_sha256)) {
            throw std::runtime_error("migration_receipt_hash_mismatch");
        }
        try {
            const auto receipt = parse_json(read_file(receipt_absolute));
            validate_receipt_identity(
                receipt, evidence.plan, requested_plan_hash, schema,
                evidence.apply_agent,
                "migration_receipt_identity_mismatch", "apply_agent_mismatch");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()) == "migration_receipt_identity_mismatch" ||
                std::string(error.what()) == "apply_agent_mismatch") {
                throw;
            }
            throw std::runtime_error("migration_receipt_identity_mismatch");
        }
    };
    if (evidence.status == "rolled_back") {
        if (!file_matches(
                receipt_absolute, receipt_operation->before_exists,
                receipt_operation->before_sha256)) {
            throw std::runtime_error("rolled_back_receipt_state_mismatch");
        }
    } else if (evidence.status == "applied") {
        validate_canonical_receipt();
    }

    const auto staged_receipt_absolute = confined_journal_path(
        transaction, receipt_operation->stage_path,
        "invalid_journal_stage_path");
    const bool recoverable = evidence.status == "prepared" ||
                             evidence.status == "applying" ||
                             evidence.status == "applied" ||
                             evidence.status == "recovery_required";
    if (recoverable) {
        if (!std::filesystem::is_regular_file(staged_receipt_absolute)) {
            throw std::runtime_error("staged_migration_receipt_missing");
        }
        if (!file_matches(
                staged_receipt_absolute, true,
                receipt_operation->after_sha256)) {
            throw std::runtime_error("staged_migration_receipt_hash_mismatch");
        }
        try {
            const auto staged_receipt =
                parse_json(read_file(staged_receipt_absolute));
            validate_receipt_identity(
                staged_receipt, evidence.plan, requested_plan_hash, schema,
                evidence.apply_agent,
                "staged_migration_receipt_identity_mismatch",
                "staged_migration_receipt_identity_mismatch");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()) ==
                "staged_migration_receipt_identity_mismatch") {
                throw;
            }
            throw std::runtime_error(
                "staged_migration_receipt_identity_mismatch");
        }
    }
    if (evidence.status != "applied" && evidence.status != "rolled_back" &&
        receipt_exists) {
        validate_canonical_receipt();
    }
    return evidence;
}

bool file_matches(
    const std::filesystem::path& path,
    bool expected_exists,
    const std::string& expected_sha256
) {
    if (!expected_exists) {
        return !std::filesystem::exists(path);
    }
    return std::filesystem::is_regular_file(path) && sha256_hex(read_file(path)) == expected_sha256;
}

void restore_before_state(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& transaction,
    const std::vector<JournalOperation>& operations,
    std::vector<std::string>& restored,
    std::vector<std::string>& failures,
    const std::optional<std::size_t>& inject_failure_after = std::nullopt,
    bool inject_reported_failure = false
) {
    std::size_t restored_count = 0;
    if (inject_failure_after && *inject_failure_after == 0) {
        throw std::runtime_error("injected_rollback_failure");
    }
    for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
        const auto path = confined_journal_path(
            backlog_root, it->path, "invalid_journal_operation_path");
        const bool before = file_matches(path, it->before_exists, it->before_sha256);
        const bool after = file_matches(path, it->after_exists, it->after_sha256);
        if (before) {
            continue;
        }
        if (!after) {
            failures.push_back("rollback_drift:" + it->path);
            continue;
        }
        if (it->before_exists) {
            const auto backup = confined_journal_path(
                transaction, it->backup_path, "invalid_journal_backup_path");
            if (!file_matches(backup, true, it->before_sha256)) {
                failures.push_back("backup_hash_mismatch:" + it->path);
                continue;
            }
            write_file_atomic(path, read_file(backup));
        } else {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) {
                failures.push_back("rollback_remove_failed:" + it->path + ":" + ec.message());
                continue;
            }
        }
        restored.push_back(it->path);
        ++restored_count;
        if (inject_reported_failure && restored_count == 1u) {
            failures.push_back("injected_rollback_reported_failure");
        }
        if (inject_failure_after && restored_count == *inject_failure_after) {
            throw std::runtime_error("injected_rollback_failure");
        }
    }
    sort_unique(restored);
    sort_unique(failures);
}

std::filesystem::path resolve_backlog_root(
    const kano::backlog_ops::PrefixMigrationOps::RecoveryOptions& options
) {
    if (options.backlog_root) {
        return normalized_absolute(*options.backlog_root);
    }
    const auto config_path = ConfigLoader::find_project_config(normalized_absolute(options.start_path));
    if (!config_path) {
        throw std::runtime_error("shared_backlog_config_not_found");
    }
    const auto root = ConfigLoader::resolve_project_root(*config_path);
    if (!root) {
        throw std::runtime_error("shared_backlog_root_not_found");
    }
    return normalized_absolute(*root);
}

void rebuild_existing_metadata_index(
    const std::filesystem::path& product_root,
    const std::string& canonical_product
) {
    auto backlog_root = product_root;
    if (product_root.parent_path().filename() == "products") {
        backlog_root = product_root.parent_path().parent_path();
    }
    const auto index_path = backlog_root / ".cache" / "index" / "backlog.db";
    if (!std::filesystem::is_regular_file(index_path)) {
        return;
    }
    BacklogIndex index(index_path, canonical_product, product_root);
    index.initialize();
    index.rebuild_metadata(product_root, canonical_product);
}

struct ParsedItem {
    std::string product;
    std::filesystem::path product_root;
    std::filesystem::path path;
    BacklogItem item;
};

void finalize_plan(PreparedPrefixMigration& prepared) {
    std::sort(prepared.plan.items.begin(), prepared.plan.items.end(), [](const auto& left, const auto& right) {
        return std::tie(left.source_id, left.source_path) < std::tie(right.source_id, right.source_path);
    });
    std::sort(prepared.plan.files.begin(), prepared.plan.files.end(), [](const auto& left, const auto& right) {
        return std::tie(left.source_path, left.target_path, left.kind) <
               std::tie(right.source_path, right.target_path, right.kind);
    });
    sort_unique(prepared.plan.resolver_checks);
    sort_unique(prepared.plan.preserved_historical_surfaces);
    sort_unique(prepared.plan.required_external_updates);
    sort_unique(prepared.plan.blockers);
    sort_unique(prepared.plan.warnings);
    prepared.plan.status = prepared.plan.blockers.empty() ? "ready" : "blocked";
    prepared.plan.plan_hash = sha256_hex(json_string(plan_json(prepared.plan, false), false));
}

PreparedPrefixMigration build_prepared(
    const kano::backlog_ops::PrefixMigrationOps::PlanOptions& options
) {
    PreparedPrefixMigration prepared;
    prepared.plan.request = options.request;
    prepared.plan.to_prefix = options.request.to_prefix;
    prepared.plan.preserved_historical_surfaces = {
        "artifact_payload_bytes",
        "duplicate_admission_payload_bytes",
        "historical_receipt_payload_bytes",
        "worklog_text",
    };

    static const std::regex prefix_regex("^[A-Z][A-Z0-9]{1,15}$");
    if (options.request.product.empty()) {
        add_blocker(prepared.plan, "product_required");
    }
    if (!std::regex_match(options.request.to_prefix, prefix_regex)) {
        add_blocker(prepared.plan, "invalid_target_prefix:expected_[A-Z][A-Z0-9]{1,15}");
    }
    if (options.request.max_files == 0 || options.request.max_files > 1000000u) {
        add_blocker(prepared.plan, "invalid_max_files");
    }
    if (options.request.max_bytes == 0 ||
        options.request.max_bytes > 256ull * 1024ull * 1024ull * 1024ull) {
        add_blocker(prepared.plan, "invalid_max_bytes");
    }

    std::optional<std::filesystem::path> config_path;
    if (options.backlog_root) {
        prepared.backlog_root = normalized_absolute(*options.backlog_root);
        config_path = prepared.backlog_root / ".kano" / "backlog_config.toml";
    } else {
        config_path = ConfigLoader::find_project_config(normalized_absolute(options.start_path));
        if (config_path) {
            const auto resolved_root = ConfigLoader::resolve_project_root(*config_path);
            if (resolved_root) {
                prepared.backlog_root = normalized_absolute(*resolved_root);
            }
        }
    }
    if (!config_path || prepared.backlog_root.empty() ||
        !std::filesystem::is_regular_file(*config_path)) {
        add_blocker(prepared.plan, "shared_backlog_config_not_found");
        finalize_plan(prepared);
        return prepared;
    }
    prepared.config_path = normalized_absolute(*config_path);
    prepared.plan.config_path = relative_path(prepared.config_path, prepared.backlog_root);

    const auto project = ProjectConfig::load_from_toml(prepared.config_path);
    if (!project) {
        add_blocker(prepared.plan, "shared_backlog_config_invalid");
        finalize_plan(prepared);
        return prepared;
    }
    const auto product_resolution = project->resolve_product(options.request.product);
    if (!product_resolution) {
        add_blocker(prepared.plan, "product_not_registered:" + options.request.product);
        finalize_plan(prepared);
        return prepared;
    }
    const auto& product_name = product_resolution->canonical_slug;
    prepared.plan.product = product_name;
    const auto product = project->get_product(product_name);
    const auto product_root =
        project->resolve_backlog_root(*product_resolution, prepared.config_path);
    if (!product || !product_root) {
        add_blocker(prepared.plan, "product_config_incomplete:" + product_name);
        finalize_plan(prepared);
        return prepared;
    }
    prepared.product_root = normalized_absolute(*product_root);
    if (!is_within(prepared.product_root, prepared.backlog_root)) {
        add_blocker(prepared.plan, "product_root_outside_shared_backlog:" + product_name);
        finalize_plan(prepared);
        return prepared;
    }
    prepared.plan.from_prefix = product->prefix;
    if (options.request.expected_from_prefix &&
        *options.request.expected_from_prefix != product->prefix) {
        add_blocker(
            prepared.plan,
            "source_prefix_mismatch:expected=" + *options.request.expected_from_prefix +
                ":actual=" + product->prefix);
    }
    if (upper_copy(product->prefix) == upper_copy(options.request.to_prefix)) {
        add_blocker(prepared.plan, "target_prefix_matches_source:" + product->prefix);
    }

    for (const auto& collision : project->find_prefix_collisions(prepared.config_path)) {
        add_blocker(
            prepared.plan,
            "existing_registry_collision:" + collision.prefix + ":" +
                collision.left_product + ":" + collision.right_product);
    }
    auto prospective = *project;
    prospective.products.at(product_name).prefix = options.request.to_prefix;
    const auto normalized_target =
        ProjectConfig::normalize_product_selector(options.request.to_prefix);
    for (const auto& collision : prospective.find_selector_collisions()) {
        if (collision.normalized_selector != normalized_target) {
            continue;
        }
        const bool involves_product = std::any_of(
            collision.claims.begin(), collision.claims.end(),
            [&](const auto& claim) { return claim.canonical_slug == product_name; });
        const bool involves_other_product = std::any_of(
            collision.claims.begin(), collision.claims.end(),
            [&](const auto& claim) { return claim.canonical_slug != product_name; });
        if (involves_product && involves_other_product) {
            add_blocker(
                prepared.plan,
                "target_selector_collision:" + collision.normalized_selector);
            break;
        }
    }
    for (const auto& [name, definition] : project->products) {
        if (name != product_name &&
            (upper_copy(definition.prefix).starts_with(upper_copy(options.request.to_prefix)) ||
             upper_copy(options.request.to_prefix).starts_with(upper_copy(definition.prefix)))) {
            prepared.plan.resolver_checks.push_back(
                "token_boundary_distinct:" + options.request.to_prefix + ":" +
                    definition.prefix + ":" + name);
        }
    }
    prepared.plan.resolver_checks.push_back("target_prefix_unique:" + options.request.to_prefix);
    prepared.plan.resolver_checks.push_back("canonical_product_slug:" + product_name);
    prepared.plan.required_external_updates.push_back(
        "repo_catalog:" + product_name + ":backlog_prefix=" + options.request.to_prefix);

    std::vector<ParsedItem> parsed_items;
    std::vector<std::filesystem::path> target_derived_items;
    std::vector<std::filesystem::path> derived_view_files;
    std::set<std::string> snapshot_paths;
    snapshot_paths.insert(prepared.plan.config_path);

    for (const auto& [name, definition] : project->products) {
        (void)definition;
        const auto root = project->resolve_backlog_root(
            trusted_canonical_product_resolution(name), prepared.config_path);
        if (!root) {
            add_blocker(prepared.plan, "product_root_unresolved:" + name);
            continue;
        }
        const auto normalized_root = normalized_absolute(*root);
        if (!is_within(normalized_root, prepared.backlog_root)) {
            add_blocker(prepared.plan, "product_root_outside_shared_backlog:" + name);
            continue;
        }
        CanonicalStore store(normalized_root);
        for (const auto& item_path : store.list_items()) {
            const auto path = normalized_absolute(item_path);
            const auto product_relative =
                path.lexically_relative(normalized_root);
            const auto top_level =
                product_relative.begin() == product_relative.end()
                    ? std::string{}
                    : product_relative.begin()->generic_string();
            if (top_level != "items" && top_level != "_trash") {
                continue;
            }
            snapshot_paths.insert(relative_path(path, prepared.backlog_root));
            if (name == product_name &&
                path.filename().generic_string().starts_with(product->prefix + "-") &&
                path.filename().generic_string().find(".index.md") != std::string::npos) {
                target_derived_items.push_back(path);
                continue;
            }
            try {
                parsed_items.push_back(ParsedItem{name, normalized_root, path, store.read(path)});
            } catch (const std::exception&) {
                if (name == product_name && path.extension() == ".md" &&
                    path.filename().generic_string().starts_with(product->prefix + "-") &&
                    path.filename().generic_string().find(".index.md") != std::string::npos) {
                    target_derived_items.push_back(path);
                } else if (name == product_name &&
                           path.filename().generic_string().starts_with(product->prefix + "-")) {
                    add_blocker(
                        prepared.plan,
                        "unparsed_target_item:" + relative_path(path, prepared.backlog_root));
                }
            }
        }
        for (const auto& path :
             regular_files_under(normalized_root / "views")) {
            const auto normalized_path = normalized_absolute(path);
            derived_view_files.push_back(normalized_path);
            snapshot_paths.insert(
                relative_path(normalized_path, prepared.backlog_root));
        }
    }
    try {
        for (const auto& path :
             regular_files_under(prepared.product_root / "items")) {
            const auto filename = path.filename().generic_string();
            if (filename.starts_with(product->prefix + "-") &&
                filename.find(".index.md") != std::string::npos) {
                target_derived_items.push_back(path);
            }
        }
        sort_unique(target_derived_items);
        for (const auto& path : regular_files_under(prepared.product_root)) {
            snapshot_paths.insert(relative_path(path, prepared.backlog_root));
        }
    } catch (const std::exception& error) {
        add_blocker(prepared.plan, error.what());
    }
    prepared.plan.source_revision = snapshot_revision(
        snapshot_paths, prepared.backlog_root, options.request, prepared.plan);

    std::map<std::string, std::vector<const ParsedItem*>> target_by_id;
    std::map<std::string, std::vector<const ParsedItem*>> target_by_uid;
    std::map<std::string, std::vector<const ParsedItem*>> global_by_id;
    for (const auto& parsed : parsed_items) {
        global_by_id[parsed.item.id].push_back(&parsed);
        if (parsed.product == product_name) {
            target_by_id[parsed.item.id].push_back(&parsed);
            target_by_uid[parsed.item.uid].push_back(&parsed);
        }
    }
    for (const auto& [id, records] : target_by_id) {
        if (records.size() > 1u) {
            std::vector<std::string> paths;
            for (const auto* record : records) {
                paths.push_back(relative_path(record->path, prepared.backlog_root));
            }
            sort_unique(paths);
            std::string joined;
            for (const auto& path : paths) {
                joined += (joined.empty() ? "" : ",") + path;
            }
            add_blocker(prepared.plan, "duplicate_source_display_id:" + id + ":" + joined);
        }
    }
    for (const auto& [uid, records] : target_by_uid) {
        if (!uid.empty() && records.size() > 1u) {
            add_blocker(prepared.plan, "duplicate_source_uid:" + uid);
        }
    }

    for (const auto& parsed : parsed_items) {
        if (parsed.product != product_name ||
            !parsed.item.id.starts_with(product->prefix + "-")) {
            continue;
        }
        const auto target_id =
            options.request.to_prefix + parsed.item.id.substr(product->prefix.size());
        const auto source_path = relative_path(parsed.path, prepared.backlog_root);
        const auto source_filename = parsed.path.filename().generic_string();
        if (!source_filename.starts_with(parsed.item.id)) {
            add_blocker(
                prepared.plan,
                "item_filename_id_mismatch:" + source_path + ":" + parsed.item.id);
            continue;
        }
        const auto target_filename =
            target_id + source_filename.substr(parsed.item.id.size());
        const auto target_path =
            relative_path(parsed.path.parent_path() / target_filename, prepared.backlog_root);
        prepared.plan.items.push_back(PrefixMigrationItemMapping{
            parsed.item.id, target_id, parsed.item.uid, source_path, target_path});
        prepared.id_mappings.emplace(parsed.item.id, target_id);
        if (global_by_id.contains(target_id)) {
            add_blocker(prepared.plan, "target_display_id_exists:" + target_id);
        }
    }
    if (prepared.plan.items.empty()) {
        add_blocker(prepared.plan, "no_source_items_for_prefix:" + product->prefix);
    }

    if (!prepared.plan.blockers.empty()) {
        finalize_plan(prepared);
        return prepared;
    }

    for (const auto& parsed : parsed_items) {
        const auto source_path = relative_path(parsed.path, prepared.backlog_root);
        const auto content = read_file(parsed.path);
        for (const auto& source_id :
             item_ids_before_worklog(content, product->prefix)) {
            if (!prepared.id_mappings.contains(source_id)) {
                prepared.plan.warnings.push_back(
                    "missing_reference_reprefixed:" + source_path + ":" +
                    source_id + ":" + options.request.to_prefix +
                    source_id.substr(product->prefix.size()));
            }
        }
        const auto updated = rewrite_product_ids(
            content, prepared.id_mappings, product->prefix,
            options.request.to_prefix, true);
        for (const auto& stale_id : item_ids_before_worklog(updated, product->prefix)) {
            add_blocker(
                prepared.plan,
                "unmapped_canonical_reference:" + source_path + ":" + stale_id);
        }
        const auto mapping = std::find_if(
            prepared.plan.items.begin(), prepared.plan.items.end(),
            [&](const auto& candidate) { return candidate.source_path == source_path; });
        if (mapping != prepared.plan.items.end()) {
            add_rename(
                prepared, mapping->source_path, mapping->target_path, "canonical_item", updated);
            add_file_change(
                prepared.plan, "rename", "canonical_item", mapping->source_path,
                mapping->target_path, true, false);
        } else if (updated != content) {
            add_mutation(prepared, source_path, "canonical_reference", updated);
            add_file_change(
                prepared.plan, "modify", "canonical_reference", source_path,
                source_path, true, false);
        }
    }
    for (const auto& path : derived_view_files) {
        const auto source_path = relative_path(path, prepared.backlog_root);
        const auto content = read_file(path);
        const auto updated = rewrite_product_ids(
            content, prepared.id_mappings, product->prefix,
            options.request.to_prefix, false);
        for (const auto& stale_id :
             item_ids_before_worklog(updated, product->prefix)) {
            add_blocker(
                prepared.plan,
                "unmapped_derived_view_reference:" + source_path + ":" +
                    stale_id);
        }
        if (updated != content) {
            add_mutation(prepared, source_path, "derived_view", updated);
            add_file_change(
                prepared.plan, "modify", "derived_view", source_path,
                source_path, true, false);
        }
    }

    for (const auto& path : target_derived_items) {
        const auto source_path = relative_path(path, prepared.backlog_root);
        const auto target_path = replace_ids_in_path(source_path, prepared.id_mappings);
        const auto content = read_file(path);
        const auto updated = rewrite_product_ids(
            content, prepared.id_mappings, product->prefix,
            options.request.to_prefix, false);
        for (const auto& stale_id : item_ids_before_worklog(updated, product->prefix)) {
            add_blocker(
                prepared.plan,
                "unmapped_derived_reference:" + source_path + ":" + stale_id);
        }
        add_rename(prepared, source_path, target_path, "derived_index", updated);
        add_file_change(
            prepared.plan, source_path == target_path ? "modify" : "rename",
            "derived_index", source_path, target_path, true, false);
    }

    const auto indexes_path = prepared.product_root / "_meta" / "indexes.md";
    if (std::filesystem::is_regular_file(indexes_path)) {
        const auto source_path = relative_path(indexes_path, prepared.backlog_root);
        const auto content = read_file(indexes_path);
        const auto updated = rewrite_product_ids(
            content, prepared.id_mappings, product->prefix,
            options.request.to_prefix, false);
        if (updated != content) {
            add_mutation(prepared, source_path, "derived_index", updated);
            add_file_change(
                prepared.plan, "modify", "derived_index", source_path,
                source_path, true, false);
        }
    }

    const auto duplicate_root =
        prepared.product_root / "_meta" / "duplicate-admission";
    for (const auto& path : regular_files_under(duplicate_root)) {
        const auto source_path = relative_path(path, prepared.backlog_root);
        const auto target_path = replace_ids_in_path(source_path, prepared.id_mappings);
        if (target_path == source_path &&
            path.stem().generic_string().starts_with(product->prefix + "-")) {
            add_blocker(prepared.plan, "orphan_duplicate_admission:" + source_path);
            continue;
        }
        if (target_path != source_path) {
            const auto content = read_file(path);
            add_rename(
                prepared, source_path, target_path, "duplicate_admission", content);
            add_file_change(
                prepared.plan, "rename", "duplicate_admission", source_path,
                target_path, false, true);
        }
    }

    const auto artifacts_root = prepared.product_root / "artifacts";
    for (const auto& path : regular_files_under(artifacts_root)) {
        const auto source_path = relative_path(path, prepared.backlog_root);
        const auto target_path = replace_ids_in_path(source_path, prepared.id_mappings);
        if (target_path != source_path) {
            const auto content = read_file(path);
            add_rename(prepared, source_path, target_path, "owned_artifact", content);
            add_file_change(
                prepared.plan, "rename", "owned_artifact", source_path,
                target_path, false, true);
        }
    }

    const auto meta_root = prepared.product_root / "_meta";
    for (const auto& path : regular_files_under(meta_root)) {
        if (path == indexes_path || is_within(path, duplicate_root)) {
            continue;
        }
        const auto source_path = relative_path(path, prepared.backlog_root);
        const auto target_path = replace_ids_in_path(source_path, prepared.id_mappings);
        if (target_path != source_path) {
            const auto content = read_file(path);
            add_rename(
                prepared, source_path, target_path, "historical_metadata", content);
            add_file_change(
                prepared.plan, "rename", "historical_metadata", source_path,
                target_path, false, true);
        }
    }

    try {
        const auto config_content = read_file(prepared.config_path);
        const auto updated_config = rewrite_config_prefix(
            config_content, product_name, product->prefix, options.request.to_prefix);
        add_mutation(
            prepared, prepared.plan.config_path, "product_config", updated_config);
        add_file_change(
            prepared.plan, "modify", "product_config", prepared.plan.config_path,
            prepared.plan.config_path, false, false);
    } catch (const std::exception& error) {
        add_blocker(prepared.plan, error.what());
    }

    const auto receipt_path = receipt_relative_path(prepared);
    if (std::filesystem::exists(prepared.backlog_root / receipt_path)) {
        add_blocker(prepared.plan, "migration_receipt_exists:" + receipt_path);
    }
    add_file_change(
        prepared.plan, "create", "migration_receipt", "", receipt_path, false, false);

    finalize_plan(prepared);
    return prepared;
}

} // namespace

namespace kano::backlog_ops {

bool PrefixMigrationPlan::ready() const {
    return status == "ready" && blockers.empty();
}

std::string PrefixMigrationPlan::to_json(bool pretty) const {
    return json_string(plan_json(*this, true), pretty);
}

std::string PrefixMigrationResult::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["changed_paths"] = string_array(changed_paths);
    value["operation_receipts"] = string_array(operation_receipts);
    value["receipt_path"] = receipt_path;
    value["recovery_status"] = recovery_status;
    value["apply_agent"] = nullable_string(apply_agent);
    value["rollback_agent"] = nullable_string(rollback_agent);
    value["rollback_mode"] = nullable_string(rollback_mode);
    value["rollback_attempted_at"] = nullable_string(rollback_attempted_at);
    value["rolled_back_at"] = nullable_string(rolled_back_at);
    value["idempotent_replay"] = idempotent_replay;
    return json_string(value, pretty);
}

std::string PrefixMigrationVerification::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["postconditions"] = string_array(postconditions);
    value["failures"] = string_array(failures);
    value["apply_agent"] = nullable_string(apply_agent);
    return json_string(value, pretty);
}

std::string PrefixMigrationStatus::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["recovery_status"] = recovery_status;
    value["apply_agent"] = nullable_string(apply_agent);
    value["rollback_agent"] = nullable_string(rollback_agent);
    value["rollback_mode"] = nullable_string(rollback_mode);
    value["rollback_attempted_at"] = nullable_string(rollback_attempted_at);
    value["rolled_back_at"] = nullable_string(rolled_back_at);
    value["rollback_supported"] = rollback_supported;
    return json_string(value, pretty);
}

std::string PrefixMigrationRollback::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["restored_paths"] = string_array(restored_paths);
    value["failures"] = string_array(failures);
    value["apply_agent"] = nullable_string(apply_agent);
    value["rollback_agent"] = nullable_string(rollback_agent);
    value["rollback_mode"] = nullable_string(rollback_mode);
    value["rollback_attempted_at"] = nullable_string(rollback_attempted_at);
    value["rolled_back_at"] = nullable_string(rolled_back_at);
    return json_string(value, pretty);
}

PrefixMigrationPlan PrefixMigrationOps::plan(const PlanOptions& options) {
    try {
        return build_prepared(options).plan;
    } catch (const std::exception& error) {
        PrefixMigrationPlan result;
        result.request = options.request;
        result.product = options.request.product;
        result.to_prefix = options.request.to_prefix;
        result.blockers.push_back(std::string("planner_error:") + error.what());
        result.status = "blocked";
        result.plan_hash = sha256_hex(json_string(plan_json(result, false), false));
        return result;
    }
}

PrefixMigrationResult PrefixMigrationOps::apply(const ApplyOptions& options) {
    PrefixMigrationResult result;
    result.plan_hash = options.expected_plan_hash;
    result.status = "blocked";
    result.recovery_status = "not_started";
    std::filesystem::path transaction;
    std::filesystem::path backlog_root;
    std::optional<std::string> apply_actor;

    try {
        apply_actor = require_mutation_agent(options.agent);
        result.apply_agent = apply_actor;
        if (!options.confirm) {
            throw std::runtime_error("confirmation_required");
        }
        if (!is_sha256(options.expected_plan_hash)) {
            throw std::runtime_error("invalid_plan_hash");
        }
        RecoveryOptions replay_recovery;
        replay_recovery.start_path = options.plan.start_path;
        replay_recovery.backlog_root = options.plan.backlog_root;
        replay_recovery.plan_hash = options.expected_plan_hash;
        backlog_root = resolve_backlog_root(replay_recovery);
        transaction = transaction_root(backlog_root, options.expected_plan_hash);
        if (std::filesystem::is_regular_file(transaction / "journal.json")) {
            const auto existing = load_validated_journal(
                backlog_root, transaction, options.expected_plan_hash);
            const auto existing_status = existing.status;
            if (existing_status == "applied") {
                const auto replay_verification = verify(replay_recovery);
                result.receipt_path = existing.receipt_path;
                result.apply_agent = existing.apply_agent;
                result.rollback_agent = existing.rollback_agent;
                result.rollback_mode = existing.rollback_mode;
                result.rollback_attempted_at = existing.rollback_attempted_at;
                result.rolled_back_at = existing.rolled_back_at;
                if (replay_verification.status != "verified") {
                    result.status = "recovery_required";
                    result.recovery_status = "required";
                    result.operation_receipts.push_back(
                        "idempotent_replay_verification_failed");
                    result.operation_receipts.insert(
                        result.operation_receipts.end(),
                        replay_verification.failures.begin(),
                        replay_verification.failures.end());
                    return result;
                }
                result.status = "applied";
                result.recovery_status = "available";
                result.idempotent_replay = true;
                result.operation_receipts = {
                    "idempotent_replay",
                    "postconditions_verified",
                };
                return result;
            }
            if (existing_status == "rolled_back") {
                std::error_code cleanup_error;
                std::filesystem::remove_all(transaction, cleanup_error);
                if (cleanup_error) {
                    throw std::runtime_error(
                        "rolled_back_transaction_cleanup_failed:" + cleanup_error.message());
                }
            } else {
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts.push_back(
                    "incomplete_transaction_requires_rollback:" + existing_status);
                return result;
            }
        }
        result.apply_agent = apply_actor;

        auto prepared = build_prepared(options.plan);
        if (prepared.backlog_root != backlog_root) {
            throw std::runtime_error("resolved_backlog_root_changed_during_apply");
        }
        if (!prepared.plan.ready()) {
            result.operation_receipts = prepared.plan.blockers;
            result.operation_receipts.push_back("plan_not_ready");
            return result;
        }
        if (prepared.plan.plan_hash != options.expected_plan_hash) {
            result.operation_receipts.push_back("stale_or_mismatched_plan_hash");
            result.operation_receipts.push_back("current_plan_hash:" + prepared.plan.plan_hash);
            return result;
        }

        const auto receipt_path = receipt_relative_path(prepared);
        Json::Value receipt(Json::objectValue);
        receipt["schema"] = kReceiptSchemaV3;
        receipt["plan_hash"] = prepared.plan.plan_hash;
        receipt["apply_agent"] = *apply_actor;
        receipt["product"] = prepared.plan.product;
        receipt["from_prefix"] = prepared.plan.from_prefix;
        receipt["to_prefix"] = prepared.plan.to_prefix;
        receipt["source_revision"] = prepared.plan.source_revision;
        receipt["compatibility_policy"] = prepared.plan.compatibility_policy;
        receipt["timestamp"] = current_utc_timestamp();
        receipt["preserved_historical_surfaces"] =
            string_array(prepared.plan.preserved_historical_surfaces);
        receipt["required_external_updates"] =
            string_array(prepared.plan.required_external_updates);
        receipt["transaction_status_ref"] =
            ".kano/cache/prefix-migrations/" + prepared.plan.plan_hash + "/journal.json";
        Json::Value item_mappings(Json::arrayValue);
        for (const auto& mapping : prepared.plan.items) {
            Json::Value value(Json::objectValue);
            value["source_id"] = mapping.source_id;
            value["target_id"] = mapping.target_id;
            value["uid"] = mapping.uid;
            value["source_path"] = mapping.source_path;
            value["target_path"] = mapping.target_path;
            item_mappings.append(value);
        }
        receipt["item_mappings"] = item_mappings;
        add_mutation(
            prepared, receipt_path, "migration_receipt",
            json_string(receipt, true) + "\n");

        std::filesystem::create_directories(transaction / "backup");
        std::filesystem::create_directories(transaction / "stage");
        std::vector<JournalOperation> journal_ops;
        std::size_t operation_index = 0;
        for (const auto& mutation : prepared.operations) {
            JournalOperation operation;
            operation.path = mutation.path;
            operation.kind = mutation.kind;
            operation.before_exists = mutation.before_exists;
            if (mutation.before_exists) {
                operation.before_sha256 = sha256_hex(mutation.before_content);
                std::ostringstream backup_name;
                backup_name << "backup/" << std::setw(8) << std::setfill('0')
                            << operation_index << ".bin";
                operation.backup_path = backup_name.str();
                write_file_atomic(
                    transaction / operation.backup_path, mutation.before_content);
            }
            operation.after_exists = mutation.after_content.has_value();
            if (mutation.after_content) {
                operation.after_sha256 = sha256_hex(*mutation.after_content);
                std::ostringstream stage_name;
                stage_name << "stage/" << std::setw(8) << std::setfill('0')
                           << operation_index << ".bin";
                operation.stage_path = stage_name.str();
                write_file_atomic(
                    transaction / operation.stage_path, *mutation.after_content);
                if (!file_matches(
                        transaction / operation.stage_path, true,
                        operation.after_sha256)) {
                    throw std::runtime_error("stage_hash_mismatch:" + mutation.path);
                }
            }
            journal_ops.push_back(std::move(operation));
            ++operation_index;
        }

        Json::Value journal(Json::objectValue);
        journal["schema"] = kJournalSchemaV3;
        journal["status"] = "prepared";
        journal["plan_hash"] = prepared.plan.plan_hash;
        journal["apply_agent"] = *apply_actor;
        journal["plan"] = parse_json(prepared.plan.to_json(false));
        journal["receipt_path"] = receipt_path;
        Json::Value operation_values(Json::arrayValue);
        for (const auto& operation : journal_ops) {
            operation_values.append(journal_operation_json(operation));
        }
        journal["operations"] = operation_values;
        write_journal(transaction, journal);

        const auto inject = [&](const std::string& phase) {
            if (options.inject_failure_after &&
                *options.inject_failure_after == phase) {
                throw std::runtime_error("injected_failure:" + phase);
            }
        };
        inject("after_stage");

        for (const auto& operation : journal_ops) {
            if (!file_matches(
                    backlog_root / operation.path, operation.before_exists,
                    operation.before_sha256)) {
                throw std::runtime_error(
                    "concurrent_drift_before_apply:" + operation.path);
            }
        }
        journal["status"] = "applying";
        write_journal(transaction, journal);

        const auto publish_kind = [&](const std::string& kind) {
            for (const auto& operation : journal_ops) {
                if (operation.kind != kind || !operation.after_exists) {
                    continue;
                }
                if (!file_matches(
                        backlog_root / operation.path, operation.before_exists,
                        operation.before_sha256)) {
                    throw std::runtime_error(
                        "concurrent_drift_before_publish:" + operation.path);
                }
                write_file_atomic(
                    backlog_root / operation.path,
                    read_file(transaction / operation.stage_path));
            }
        };
        publish_kind("canonical_item");
        publish_kind("canonical_reference");
        publish_kind("derived_view");
        publish_kind("derived_index");
        publish_kind("duplicate_admission");
        publish_kind("owned_artifact");
        publish_kind("historical_metadata");
        inject("after_content_publish");

        publish_kind("product_config");
        inject("after_config_publish");

        for (const auto& operation : journal_ops) {
            if (operation.after_exists) {
                continue;
            }
            if (!file_matches(
                    backlog_root / operation.path, operation.before_exists,
                    operation.before_sha256)) {
                throw std::runtime_error(
                    "concurrent_drift_before_retire:" + operation.path);
            }
            std::error_code remove_error;
            std::filesystem::remove(backlog_root / operation.path, remove_error);
            if (remove_error) {
                throw std::runtime_error(
                    "source_retire_failed:" + operation.path + ":" +
                    remove_error.message());
            }
        }
        inject("after_source_retire");

        publish_kind("migration_receipt");
        for (const auto& operation : journal_ops) {
            if (!file_matches(
                    backlog_root / operation.path, operation.after_exists,
                    operation.after_sha256)) {
                throw std::runtime_error("post_write_hash_mismatch:" + operation.path);
            }
            result.changed_paths.push_back(operation.path);
        }
        sort_unique(result.changed_paths);
        journal["status"] = "applied";
        write_journal(transaction, journal);

        RecoveryOptions recovery;
        recovery.backlog_root = backlog_root;
        recovery.plan_hash = prepared.plan.plan_hash;
        const auto verification = verify(recovery);
        if (verification.status != "verified") {
            std::string failures;
            for (const auto& failure : verification.failures) {
                failures += (failures.empty() ? "" : ",") + failure;
            }
            throw std::runtime_error(
                "postcondition_verification_failed:" + failures);
        }
        rebuild_existing_metadata_index(
            prepared.product_root, prepared.plan.product);

        result.status = "applied";
        result.recovery_status = "available";
        result.receipt_path = receipt_path;
        result.operation_receipts = {
            "reviewed_plan_hash_matched",
            "staged_outputs_verified",
            "canonical_items_and_refs_published",
            "historical_evidence_bytes_preserved",
            "source_paths_retired",
            "postconditions_verified",
        };
        return result;
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(error.what());
        if (!transaction.empty() &&
            std::filesystem::is_regular_file(transaction / "journal.json")) {
            Json::Value recovery_journal;
            std::optional<std::size_t> recovery_attempt_index;
            bool recovery_attempt_persisted = false;
            std::vector<std::string> automatic_recovery_failures;
            try {
                auto evidence = load_validated_journal(
                    backlog_root, transaction, options.expected_plan_hash);
                recovery_journal = evidence.journal;
                const auto rollback_attempted_at = current_utc_timestamp();
                recovery_journal["status"] = "recovery_required";
                recovery_attempt_index = append_rollback_attempt(
                    recovery_journal, *apply_actor, "automatic",
                    rollback_attempted_at);
                recovery_journal["last_error"] =
                    bounded_recovery_error(error.what());
                write_journal(transaction, recovery_journal);
                recovery_attempt_persisted = true;
                result.rollback_agent = apply_actor;
                result.rollback_mode = "automatic";
                result.rollback_attempted_at = rollback_attempted_at;
                const bool inject_chained_failure =
                    options.inject_automatic_recovery_failure ==
                    std::optional<std::string>(
                        "reported_failure_then_exception");
                if (options.inject_automatic_recovery_failure &&
                    !inject_chained_failure) {
                    throw std::runtime_error(
                        "invalid_automatic_recovery_failure_injection");
                }
                restore_before_state(
                    backlog_root, transaction, evidence.operations,
                    result.changed_paths, automatic_recovery_failures,
                    options.inject_rollback_failure_after,
                    inject_chained_failure);
                if (inject_chained_failure) {
                    throw std::runtime_error(
                        "injected_automatic_recovery_exception_after_restore");
                }
                if (automatic_recovery_failures.empty()) {
                    rebuild_existing_metadata_index(
                        evidence.product_root,
                        evidence.plan["product"].asString());
                    recovery_journal["status"] = "rolled_back";
                    const auto rolled_back_at = mark_rollback_attempt_completed(
                        recovery_journal, *recovery_attempt_index);
                    recovery_journal.removeMember("last_error");
                    write_journal(transaction, recovery_journal);
                    result.rolled_back_at = rolled_back_at;
                    result.status = "rolled_back";
                    result.recovery_status = "completed";
                    result.operation_receipts.push_back(
                        "automatic_rollback_completed");
                } else {
                    const auto merged_failures =
                        merged_recovery_failures(automatic_recovery_failures);
                    mark_rollback_attempt_failed(
                        recovery_journal, *recovery_attempt_index,
                        merged_failures);
                    recovery_journal["last_error"] = merged_failures;
                    write_journal(transaction, recovery_journal);
                    result.status = "recovery_required";
                    result.recovery_status = "required";
                    result.operation_receipts.insert(
                        result.operation_receipts.end(),
                        automatic_recovery_failures.begin(),
                        automatic_recovery_failures.end());
                }
            } catch (const std::exception& recovery_error) {
                result.status = "recovery_required";
                result.recovery_status = "required";
                automatic_recovery_failures.push_back(
                    "automatic_rollback_failed:" +
                    bounded_recovery_error(recovery_error.what()));
                result.operation_receipts.insert(
                    result.operation_receipts.end(),
                    automatic_recovery_failures.begin(),
                    automatic_recovery_failures.end());
                if (recovery_attempt_persisted && recovery_attempt_index) {
                    try {
                        recovery_journal["status"] = "recovery_required";
                        recovery_journal.removeMember("rolled_back_at");
                        const auto merged_failures =
                            merged_recovery_failures(
                                automatic_recovery_failures);
                        mark_rollback_attempt_failed(
                            recovery_journal, *recovery_attempt_index,
                            merged_failures);
                        recovery_journal["last_error"] = merged_failures;
                        write_journal(transaction, recovery_journal);
                    } catch (const std::exception& update_error) {
                        result.operation_receipts.push_back(
                            "automatic_rollback_evidence_update_failed:" +
                            bounded_recovery_error(update_error.what()));
                    }
                }
            }
        }
        sort_unique(result.changed_paths);
        sort_unique(result.operation_receipts);
        return result;
    }
}

PrefixMigrationVerification PrefixMigrationOps::verify(
    const RecoveryOptions& options
) {
    PrefixMigrationVerification verification;
    verification.plan_hash = options.plan_hash;
    verification.status = "not_applied";
    try {
        reject_read_only_agent(options.agent);
        const auto backlog_root = resolve_backlog_root(options);
        const auto transaction = transaction_root(backlog_root, options.plan_hash);
        const auto evidence = load_validated_journal(
            backlog_root, transaction, options.plan_hash);
        verification.apply_agent = evidence.apply_agent;
        if (evidence.status != "applied") {
            verification.failures.push_back(
                "migration_not_applied:" + evidence.status);
        }
        const auto& embedded_plan = evidence.plan;

        for (const auto& operation : evidence.operations) {
            if (!file_matches(
                    confined_journal_path(
                        backlog_root, operation.path,
                        "invalid_journal_operation_path"),
                    operation.after_exists,
                    operation.after_sha256)) {
                verification.failures.push_back(
                    "operation_postcondition_failed:" + operation.path);
            }
        }
        if (verification.failures.empty()) {
            verification.postconditions.push_back("journal_file_hashes_match");
        }

        const auto config_path = confined_journal_path(
            backlog_root, embedded_plan["config_path"].asString(),
            "invalid_embedded_plan_config_path");
        const auto project = ProjectConfig::load_from_toml(config_path);
        const auto product_name = embedded_plan["product"].asString();
        const auto from_prefix = embedded_plan["from_prefix"].asString();
        const auto to_prefix = embedded_plan["to_prefix"].asString();
        if (!project) {
            verification.failures.push_back("post_migration_config_invalid");
        } else {
            const auto product = project->get_product(product_name);
            if (!product || product->prefix != to_prefix) {
                verification.failures.push_back("target_prefix_not_registered");
            }
            if (project->resolve_product_name(to_prefix) !=
                std::optional<std::string>(product_name)) {
                verification.failures.push_back("target_prefix_resolver_failed");
            }
            if (project->resolve_product_name(from_prefix)) {
                verification.failures.push_back("legacy_prefix_alias_unexpectedly_resolves");
            }
            if (!project->find_prefix_collisions(config_path).empty()) {
                verification.failures.push_back("post_migration_prefix_collision");
            }
        }

        if (project) {
            CanonicalStore store(evidence.product_root);
            for (const auto& mapping : embedded_plan["items"]) {
                const auto source_id = mapping["source_id"].asString();
                const auto target_id = mapping["target_id"].asString();
                if (store.find_item_path_by_id(source_id)) {
                    verification.failures.push_back(
                        "source_id_still_resolves:" + source_id);
                }
                const auto target_path = confined_journal_path(
                    backlog_root, mapping["target_path"].asString(),
                    "invalid_embedded_plan_item_path");
                try {
                    const auto item = store.read(target_path);
                    if (item.id != target_id ||
                        item.uid != mapping["uid"].asString()) {
                        verification.failures.push_back(
                            "item_identity_mismatch:" + target_id);
                    }
                } catch (const std::exception&) {
                    verification.failures.push_back(
                        "target_item_unreadable:" + target_id);
                }
            }

            for (const auto& [name, definition] : project->products) {
                (void)definition;
                const auto root = project->resolve_backlog_root(
                    trusted_canonical_product_resolution(name), config_path);
                if (!root) {
                    continue;
                }
                CanonicalStore store(*root);
                for (const auto& path : store.list_items()) {
                    try {
                        const auto content = read_file(path);
                        for (const auto& stale :
                             item_ids_before_worklog(content, from_prefix)) {
                            verification.failures.push_back(
                                "stale_canonical_reference:" +
                                relative_path(path, backlog_root) + ":" + stale);
                        }
                    } catch (const std::exception&) {
                        // Derived legacy Markdown is covered by journal hashes.
                    }
                }
            }
        }
        if (verification.failures.empty()) {
            if (evidence.journal["schema"].asString() == kJournalSchemaV3) {
                verification.postconditions.push_back(
                    "apply_actor_provenance_verified");
            }
            verification.postconditions.push_back(
                "uid_and_display_id_mapping_verified");
            verification.postconditions.push_back(
                "canonical_refs_rewritten_outside_worklog");
            verification.postconditions.push_back(
                "target_prefix_resolves_without_legacy_alias");
            verification.postconditions.push_back(
                "historical_evidence_hashes_match");
            verification.status = "verified";
        } else {
            verification.status = "failed";
        }
    } catch (const std::exception& error) {
        verification.failures.push_back(error.what());
        verification.status =
            std::string(error.what()) == "prefix_migration_journal_not_found"
                ? "not_applied"
                : "failed";
    }
    sort_unique(verification.postconditions);
    sort_unique(verification.failures);
    return verification;
}

PrefixMigrationStatus PrefixMigrationOps::status(
    const RecoveryOptions& options
) {
    PrefixMigrationStatus result;
    result.plan_hash = options.plan_hash;
    result.status = "not_applied";
    result.recovery_status = "none";
    try {
        reject_read_only_agent(options.agent);
        const auto backlog_root = resolve_backlog_root(options);
        const auto transaction =
            transaction_root(backlog_root, options.plan_hash);
        const auto evidence = load_validated_journal(
            backlog_root, transaction, options.plan_hash);
        result.apply_agent = evidence.apply_agent;
        result.rollback_agent = evidence.rollback_agent;
        result.rollback_mode = evidence.rollback_mode;
        result.rollback_attempted_at = evidence.rollback_attempted_at;
        result.rolled_back_at = evidence.rolled_back_at;
        result.status = evidence.status;
        if (result.status == "applied") {
            result.recovery_status = "available";
            result.rollback_supported = true;
        } else if (
            result.status == "prepared" || result.status == "applying" ||
            result.status == "recovery_required") {
            result.recovery_status = "required";
            result.rollback_supported = true;
        } else if (result.status == "rolled_back") {
            result.recovery_status = "completed";
        }
    } catch (const std::exception& error) {
        if (std::string(error.what()) != "prefix_migration_journal_not_found") {
            result.status = "failed";
            result.recovery_status = error.what();
        }
    }
    return result;
}

PrefixMigrationRollback PrefixMigrationOps::rollback(
    const RecoveryOptions& options
) {
    PrefixMigrationRollback result;
    result.plan_hash = options.plan_hash;
    result.status = "blocked";
    std::string rollback_actor;
    try {
        rollback_actor = require_mutation_agent(options.agent);
    } catch (const std::exception& error) {
        result.failures.push_back(bounded_recovery_error(error.what()));
        return result;
    }
    if (!options.confirm) {
        result.failures.push_back("confirmation_required");
        return result;
    }
    std::filesystem::path transaction;
    Json::Value journal;
    bool attempt_persisted = false;
    std::optional<std::size_t> attempt_index;
    try {
        const auto backlog_root = resolve_backlog_root(options);
        transaction = transaction_root(backlog_root, options.plan_hash);
        const auto evidence = load_validated_journal(
            backlog_root, transaction, options.plan_hash);
        result.apply_agent = evidence.apply_agent;
        if (evidence.status == "rolled_back") {
            rebuild_existing_metadata_index(
                evidence.product_root,
                evidence.plan["product"].asString());
            result.rollback_agent = evidence.rollback_agent;
            result.rollback_mode = evidence.rollback_mode;
            result.rollback_attempted_at = evidence.rollback_attempted_at;
            result.rolled_back_at = evidence.rolled_back_at;
            result.status = "rolled_back";
            return result;
        }
        journal = evidence.journal;
        const auto rollback_attempted_at = current_utc_timestamp();
        journal["status"] = "recovery_required";
        attempt_index = append_rollback_attempt(
            journal, rollback_actor, "manual", rollback_attempted_at);
        journal["last_error"] = "manual_rollback_in_progress";
        write_journal(transaction, journal);
        attempt_persisted = true;
        result.rollback_agent = rollback_actor;
        result.rollback_mode = "manual";
        result.rollback_attempted_at = rollback_attempted_at;
        restore_before_state(
            backlog_root, transaction, evidence.operations,
            result.restored_paths, result.failures,
            options.inject_rollback_failure_after);
        if (result.failures.empty()) {
            rebuild_existing_metadata_index(
                evidence.product_root,
                evidence.plan["product"].asString());
            journal["status"] = "rolled_back";
            const auto rolled_back_at = mark_rollback_attempt_completed(
                journal, *attempt_index);
            journal.removeMember("last_error");
            write_journal(transaction, journal);
            result.rolled_back_at = rolled_back_at;
            result.status = "rolled_back";
        } else {
            mark_rollback_attempt_failed(
                journal, *attempt_index, result.failures.front());
            journal["last_error"] =
                bounded_recovery_error(result.failures.front());
            write_journal(transaction, journal);
            result.status = "recovery_required";
        }
    } catch (const std::exception& error) {
        result.failures.push_back(bounded_recovery_error(error.what()));
        result.status = attempt_persisted ? "recovery_required" : "failed";
        if (attempt_persisted && attempt_index) {
            try {
                journal["status"] = "recovery_required";
                journal.removeMember("rolled_back_at");
                mark_rollback_attempt_failed(
                    journal, *attempt_index, error.what());
                journal["last_error"] = bounded_recovery_error(error.what());
                write_journal(transaction, journal);
            } catch (const std::exception& update_error) {
                result.failures.push_back(
                    "rollback_evidence_update_failed:" +
                    bounded_recovery_error(update_error.what()));
            }
        }
    }
    sort_unique(result.restored_paths);
    sort_unique(result.failures);
    return result;
}

} // namespace kano::backlog_ops
