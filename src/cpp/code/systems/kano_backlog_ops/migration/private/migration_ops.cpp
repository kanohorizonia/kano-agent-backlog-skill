#include "kano/backlog_ops/migration/migration_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"

#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
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
using kano::backlog_core::ItemType;
using kano::backlog_core::ProjectConfig;
using kano::backlog_ops::MigrationArtifactMapping;
using kano::backlog_ops::MigrationItemMapping;
using kano::backlog_ops::MigrationPlan;
using kano::backlog_ops::MigrationReferenceRewrite;
using kano::backlog_ops::MigrationRequest;

constexpr std::size_t kMaximumMigrationReferenceFiles = 500000;
constexpr std::uintmax_t kMaximumMigrationReferenceBytes = 64ull * 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumMigrationReferences = 500000;
constexpr std::size_t kMaximumMigrationInventoryItems = 500000;
constexpr std::uintmax_t kMaximumMigrationArtifactBytes = 256ull * 1024ull * 1024ull * 1024ull;
constexpr std::uintmax_t kMaximumMigrationMaterializationBytes = 512ull * 1024ull * 1024ull * 1024ull;
constexpr std::uintmax_t kMaximumReferenceFileBytes = 16ull * 1024ull * 1024ull;

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
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
        throw std::runtime_error("Migration path escapes backlog root: " + path.generic_string());
    }
    return normalized_path.lexically_relative(normalized_root).generic_string();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to read migration input: " + path.generic_string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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
        std::fill(
            buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
            buffer_.begin() + 56,
            0u);
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
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
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
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
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

struct BoundedFileRead {
    std::uintmax_t bytes = 0;
    bool limit_exceeded = false;
};

BoundedFileRead hash_file_into(
    const std::filesystem::path& path,
    std::uintmax_t max_bytes,
    StreamingSha256& hasher
) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to read migration input: " + path.generic_string());
    }
    BoundedFileRead result;
    std::array<char, 64u * 1024u> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        const auto bytes = static_cast<std::uintmax_t>(count);
        if (bytes > max_bytes - result.bytes) {
            result.limit_exceeded = true;
            return result;
        }
        hasher.update(buffer.data(), static_cast<std::size_t>(count));
        result.bytes += bytes;
    }
    if (input.bad()) {
        throw std::runtime_error("Unable to read migration input: " + path.generic_string());
    }
    return result;
}

std::string snapshot_hash(std::vector<std::filesystem::path> paths, const std::filesystem::path& root) {
    std::sort(paths.begin(), paths.end(), [&](const auto& left, const auto& right) {
        return relative_path(left, root) < relative_path(right, root);
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    StreamingSha256 hasher;
    for (const auto& path : paths) {
        if (!std::filesystem::is_regular_file(path)) {
            continue;
        }
        const auto rel = relative_path(path, root);
        const auto file_bytes = std::filesystem::file_size(path);
        hasher.update(
            std::to_string(rel.size()) + ':' + rel + ':' + std::to_string(file_bytes) + ':');
        const auto read = hash_file_into(path, file_bytes, hasher);
        if (read.limit_exceeded || read.bytes != file_bytes) {
            throw std::runtime_error("snapshot_input_changed:" + rel);
        }
    }
    return hasher.final_hex();
}

std::string json_string(const Json::Value& value, bool pretty) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = pretty ? "  " : "";
    builder["commentStyle"] = "None";
    return Json::writeString(builder, value);
}

Json::Value strings_json(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) {
        result.append(value);
    }
    return result;
}

Json::Value request_json(const MigrationRequest& request) {
    Json::Value value(Json::objectValue);
    value["schema"] = request.schema;
    value["source_product"] = request.source_product;
    value["source_ref"] = request.source_ref;
    value["target_product"] = request.target_product;
    value["scope"] = request.scope;
    value["include_owned_artifacts"] = request.include_owned_artifacts;
    value["max_items"] = static_cast<Json::UInt64>(request.max_items);
    value["max_artifacts"] = static_cast<Json::UInt64>(request.max_artifacts);
    value["max_source_inventory_items"] = static_cast<Json::UInt64>(request.max_source_inventory_items);
    value["max_target_inventory_items"] = static_cast<Json::UInt64>(request.max_target_inventory_items);
    value["max_artifact_bytes"] = static_cast<Json::UInt64>(request.max_artifact_bytes);
    value["max_materialization_bytes"] = static_cast<Json::UInt64>(request.max_materialization_bytes);
    value["max_reference_files"] = static_cast<Json::UInt64>(request.max_reference_files);
    value["max_reference_bytes"] = static_cast<Json::UInt64>(request.max_reference_bytes);
    value["max_references"] = static_cast<Json::UInt64>(request.max_references);
    if (request.expected_source_revision) {
        value["expected_source_revision"] = *request.expected_source_revision;
    }
    if (request.expected_target_prefix) {
        value["expected_target_prefix"] = *request.expected_target_prefix;
    }
    return value;
}

Json::Value plan_json(const MigrationPlan& plan, bool include_hash) {
    Json::Value value(Json::objectValue);
    value["schema"] = plan.schema;
    value["status"] = plan.status;
    value["request"] = request_json(plan.request);
    value["source_revision"] = plan.source_revision;
    value["target_revision"] = plan.target_revision;
    value["source_root_id"] = plan.source_root_id;
    value["source_root_uid"] = plan.source_root_uid;
    value["target_prefix"] = plan.target_prefix;
    value["dry_run"] = plan.dry_run;
    value["mutates_backlog"] = plan.mutates_backlog;

    Json::Value items(Json::arrayValue);
    for (const auto& mapping : plan.items) {
        Json::Value item(Json::objectValue);
        item["source_id"] = mapping.source_id;
        item["target_id"] = mapping.target_id;
        item["uid"] = mapping.uid;
        item["type"] = kano::backlog_core::to_string(mapping.type);
        item["source_path"] = mapping.source_path;
        item["target_path"] = mapping.target_path;
        items.append(item);
    }
    value["items"] = items;

    Json::Value artifacts(Json::arrayValue);
    for (const auto& mapping : plan.artifacts) {
        Json::Value artifact(Json::objectValue);
        artifact["owner_source_id"] = mapping.owner_source_id;
        artifact["owner_target_id"] = mapping.owner_target_id;
        artifact["source_path"] = mapping.source_path;
        artifact["target_path"] = mapping.target_path;
        artifact["sha256"] = mapping.sha256;
        artifact["size_bytes"] = static_cast<Json::UInt64>(mapping.size_bytes);
        artifacts.append(artifact);
    }
    value["artifacts"] = artifacts;

    Json::Value references(Json::arrayValue);
    for (const auto& rewrite : plan.references) {
        Json::Value reference(Json::objectValue);
        reference["path"] = rewrite.path;
        reference["reference_class"] = rewrite.reference_class;
        reference["source_id"] = rewrite.source_id;
        reference["target_id"] = rewrite.target_id;
        reference["occurrence_count"] = static_cast<Json::UInt64>(rewrite.occurrence_count);
        reference["external_to_subtree"] = rewrite.external_to_subtree;
        references.append(reference);
    }
    value["references"] = references;
    value["affected_paths"] = strings_json(plan.affected_paths);
    value["blockers"] = strings_json(plan.blockers);
    value["warnings"] = strings_json(plan.warnings);
    if (include_hash) {
        value["plan_hash"] = plan.plan_hash;
    }
    return value;
}

std::string type_code(ItemType type) {
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
    throw std::runtime_error("Unsupported migration item type");
}

std::optional<int> display_id_number(
    const std::string& id,
    const std::string& prefix,
    ItemType type
) {
    const auto expected_prefix = prefix + "-" + type_code(type) + "-";
    if (!id.starts_with(expected_prefix) || id.size() == expected_prefix.size()) {
        return std::nullopt;
    }
    int number = 0;
    for (std::size_t index = expected_prefix.size(); index < id.size(); ++index) {
        const auto ch = static_cast<unsigned char>(id[index]);
        if (!std::isdigit(ch) || number > (std::numeric_limits<int>::max() - (ch - '0')) / 10) {
            return std::nullopt;
        }
        number = number * 10 + (ch - '0');
    }
    return number;
}

std::map<std::string, std::size_t> mapped_id_occurrences(
    const std::string& content,
    const std::unordered_map<std::string, std::string>& id_mapping
) {
    const auto is_id_character = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_';
    };
    std::map<std::string, std::size_t> occurrences;
    std::size_t offset = 0;
    while (offset < content.size()) {
        if (!is_id_character(static_cast<unsigned char>(content[offset]))) {
            ++offset;
            continue;
        }
        const auto begin = offset;
        while (offset < content.size() && is_id_character(static_cast<unsigned char>(content[offset]))) {
            ++offset;
        }
        const auto token = content.substr(begin, offset - begin);
        if (id_mapping.contains(token)) {
            ++occurrences[token];
        }
    }
    return occurrences;
}

std::string replace_migration_ids(
    std::string content,
    const std::unordered_map<std::string, std::string>& id_mapping
) {
    const auto is_id_character = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_';
    };
    std::string rewritten;
    rewritten.reserve(content.size());
    std::size_t offset = 0;
    while (offset < content.size()) {
        if (!is_id_character(static_cast<unsigned char>(content[offset]))) {
            rewritten.push_back(content[offset++]);
            continue;
        }
        const auto begin = offset;
        while (offset < content.size() && is_id_character(static_cast<unsigned char>(content[offset]))) {
            ++offset;
        }
        const auto token = content.substr(begin, offset - begin);
        if (const auto mapping = id_mapping.find(token); mapping != id_mapping.end()) {
            rewritten += mapping->second;
        } else {
            rewritten += token;
        }
    }
    return rewritten;
}

bool is_supported_text_reference(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    return extension == ".md" || extension == ".json" || extension == ".toml" ||
           extension == ".yaml" || extension == ".yml" || extension == ".txt";
}

bool is_derived_or_internal_path(const std::filesystem::path& path) {
    for (const auto& component : path) {
        const auto value = component.string();
        if (value == ".cache" || value == "views" || value == ".git") {
            return true;
        }
    }
    return false;
}

struct BoundedFileScan {
    std::vector<std::filesystem::path> files;
    bool file_limit_exceeded = false;
    bool entry_limit_exceeded = false;
};

BoundedFileScan bounded_files_under(
    const std::vector<std::filesystem::path>& roots,
    const std::filesystem::path& backlog_root,
    std::size_t max_files,
    const std::unordered_set<std::string>& excluded_paths = {},
    const std::optional<std::string>& required_extension = std::nullopt,
    bool exclude_derived_or_internal = false
) {
    BoundedFileScan result;
    const std::size_t max_entries = max_files * 4u + 64u;
    std::size_t visited_entries = 0;
    std::vector<std::filesystem::path> pending;
    const auto normalized_backlog_root = normalized_absolute(backlog_root);
    for (const auto& root : roots) {
        if (std::filesystem::is_symlink(root)) {
            throw std::runtime_error("bounded_scan_symlink_not_supported:" + root.generic_string());
        }
        const auto normalized_root = normalized_absolute(root);
        if (normalized_root != normalized_backlog_root && !is_within(normalized_root, normalized_backlog_root)) {
            throw std::runtime_error("bounded_scan_path_escape:" + root.generic_string());
        }
        if (std::filesystem::is_directory(normalized_root)) {
            pending.push_back(normalized_root);
        }
    }
    while (!pending.empty()) {
        const auto directory = pending.back();
        pending.pop_back();
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (visited_entries >= max_entries) {
                result.files.clear();
                result.entry_limit_exceeded = true;
                return result;
            }
            ++visited_entries;
            const auto path = entry.path();
            const auto relative = path.lexically_relative(backlog_root);
            const auto rel = relative.generic_string();
            if (relative.empty() || relative.is_absolute() || rel == ".." || rel.starts_with("../")) {
                throw std::runtime_error("bounded_scan_path_escape:" + path.generic_string());
            }
            if (entry.is_symlink()) {
                throw std::runtime_error("bounded_scan_symlink_not_supported:" + rel);
            }
            if (exclude_derived_or_internal && is_derived_or_internal_path(relative)) {
                continue;
            }
            if (entry.is_directory()) {
                pending.push_back(path);
                continue;
            }
            if (!entry.is_regular_file() || excluded_paths.contains(rel) ||
                (required_extension && path.extension() != *required_extension)) {
                continue;
            }
            if (result.files.size() >= max_files) {
                result.files.clear();
                result.file_limit_exceeded = true;
                return result;
            }
            result.files.push_back(path);
        }
    }
    std::sort(result.files.begin(), result.files.end());
    return result;
}

std::filesystem::path resolve_backlog_root(
    const kano::backlog_ops::MigrationOps::PlanOptions& options,
    const std::filesystem::path& config_path
) {
    if (options.backlog_root) {
        return normalized_absolute(*options.backlog_root);
    }
    const auto project_root = ConfigLoader::resolve_project_root(config_path);
    if (!project_root) {
        throw std::runtime_error("Unable to resolve project root from backlog configuration");
    }
    return normalized_absolute(*project_root / "_kano" / "backlog");
}

void sort_unique(std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

bool is_sha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) || (ch >= 'a' && ch <= 'f');
    });
}

Json::Value parse_json(const std::string& content) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    Json::Value value;
    std::string errors;
    std::istringstream input(content);
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("Invalid migration journal JSON: " + errors);
    }
    return value;
}

void write_file_atomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".kob-migration.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("Unable to stage migration output: " + temporary.generic_string());
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output.good()) {
            throw std::runtime_error("Unable to write migration output: " + temporary.generic_string());
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Unable to publish migration output: " + path.generic_string() + ":" + ec.message());
    }
}

struct MigrationFileOperation {
    std::string path;
    std::string kind;
    bool before_exists = false;
    std::string before_sha256;
    bool after_exists = false;
    std::string after_sha256;
    std::string backup_path;
    std::string stage_path;
    std::optional<std::string> after_content;
};

Json::Value operation_json(const MigrationFileOperation& operation) {
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

MigrationFileOperation operation_from_json(const Json::Value& value) {
    MigrationFileOperation operation;
    operation.path = value["path"].asString();
    operation.kind = value["kind"].asString();
    operation.before_exists = value["before_exists"].asBool();
    operation.before_sha256 = value["before_sha256"].asString();
    operation.after_exists = value["after_exists"].asBool();
    operation.after_sha256 = value["after_sha256"].asString();
    operation.backup_path = value["backup_path"].asString();
    operation.stage_path = value["stage_path"].asString();
    return operation;
}

std::filesystem::path transaction_root(const std::filesystem::path& backlog_root, const std::string& plan_hash) {
    if (!is_sha256(plan_hash)) {
        throw std::runtime_error("invalid_plan_hash");
    }
    return backlog_root / ".cache" / "migrations" / plan_hash;
}

std::filesystem::path resolve_recovery_backlog_root(
    const kano::backlog_ops::MigrationOps::RecoveryOptions& options
) {
    if (options.backlog_root) {
        return normalized_absolute(*options.backlog_root);
    }
    const auto config_path = ConfigLoader::find_project_config(normalized_absolute(options.start_path));
    if (!config_path) {
        throw std::runtime_error("project_config_not_found");
    }
    const auto project_root = ConfigLoader::resolve_project_root(*config_path);
    if (!project_root) {
        throw std::runtime_error("project_root_not_found");
    }
    return normalized_absolute(*project_root / "_kano" / "backlog");
}

Json::Value load_journal(const std::filesystem::path& transaction) {
    const auto path = transaction / "journal.json";
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("migration_journal_not_found");
    }
    const auto journal = parse_json(read_file(path));
    if (journal["schema"].asString() != "kob.cross_product_migration.journal.v1") {
        throw std::runtime_error("unsupported_migration_journal_schema");
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
) {
    if (!expected_exists) {
        return !std::filesystem::exists(path);
    }
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }
    const auto file_bytes = std::filesystem::file_size(path);
    StreamingSha256 hasher;
    const auto read = hash_file_into(path, file_bytes, hasher);
    return !read.limit_exceeded && read.bytes == file_bytes && hasher.final_hex() == expected_sha256;
}

std::vector<MigrationFileOperation> journal_operations(const Json::Value& journal) {
    std::vector<MigrationFileOperation> operations;
    for (const auto& value : journal["operations"]) {
        operations.push_back(operation_from_json(value));
    }
    return operations;
}

std::vector<std::string> restore_before_state(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& transaction,
    const std::vector<MigrationFileOperation>& operations,
    std::vector<std::string>& failures
) {
    std::vector<std::string> restored;
    for (const auto& operation : operations) {
        const auto path = backlog_root / operation.path;
        const bool before = file_matches(path, operation.before_exists, operation.before_sha256);
        const bool after = file_matches(path, operation.after_exists, operation.after_sha256);
        if (!before && !after) {
            failures.push_back("rollback_drift:" + operation.path);
        }
    }
    if (!failures.empty()) {
        return restored;
    }
    for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
        const auto path = backlog_root / it->path;
        try {
            if (it->before_exists) {
                const auto backup = transaction / it->backup_path;
                if (!file_matches(backup, true, it->before_sha256)) {
                    failures.push_back("backup_hash_mismatch:" + it->path);
                    continue;
                }
                write_file_atomic(path, read_file(backup));
            } else {
                std::error_code ec;
                std::filesystem::remove(path, ec);
                if (ec) {
                    failures.push_back("remove_failed:" + it->path + ":" + ec.message());
                    continue;
                }
            }
            restored.push_back(it->path);
        } catch (const std::exception& error) {
            failures.push_back("restore_failed:" + it->path + ":" + error.what());
        }
    }
    for (const auto& operation : operations) {
        if (!file_matches(backlog_root / operation.path, operation.before_exists, operation.before_sha256)) {
            failures.push_back("restore_verification_failed:" + operation.path);
        }
    }
    sort_unique(restored);
    sort_unique(failures);
    return restored;
}

} // namespace

namespace kano::backlog_ops {

bool MigrationPlan::ready() const {
    return status == "ready" && blockers.empty() && !plan_hash.empty();
}

std::string MigrationPlan::to_json(bool pretty) const {
    return json_string(plan_json(*this, true), pretty);
}

std::string MigrationResult::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["changed_paths"] = strings_json(changed_paths);
    value["operation_receipts"] = strings_json(operation_receipts);
    value["recovery_status"] = recovery_status;
    value["idempotent_replay"] = idempotent_replay;
    return json_string(value, pretty);
}

std::string MigrationVerification::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["postconditions"] = strings_json(postconditions);
    value["failures"] = strings_json(failures);
    return json_string(value, pretty);
}

std::string MigrationStatus::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["recovery_status"] = recovery_status;
    value["resume_supported"] = resume_supported;
    value["rollback_supported"] = rollback_supported;
    return json_string(value, pretty);
}

std::string MigrationRollback::to_json(bool pretty) const {
    Json::Value value(Json::objectValue);
    value["schema"] = schema;
    value["status"] = status;
    value["plan_hash"] = plan_hash;
    value["restored_paths"] = strings_json(restored_paths);
    value["failures"] = strings_json(failures);
    return json_string(value, pretty);
}

std::vector<std::string> MigrationOps::validate_request(const MigrationRequest& request) {
    std::vector<std::string> diagnostics;
    if (request.schema != kMigrationRequestSchema) {
        diagnostics.push_back("unsupported_request_schema");
    }
    if (trim(request.source_product).empty()) {
        diagnostics.push_back("source_product_required");
    }
    if (trim(request.source_ref).empty()) {
        diagnostics.push_back("source_ref_required");
    }
    if (trim(request.target_product).empty()) {
        diagnostics.push_back("target_product_required");
    }
    if (!request.source_product.empty() && request.source_product == request.target_product) {
        diagnostics.push_back("source_and_target_product_must_differ");
    }
    if (request.scope != "subtree") {
        diagnostics.push_back("unsupported_scope");
    }
    if (request.max_items == 0 || request.max_items > 100000) {
        diagnostics.push_back("max_items_out_of_range");
    }
    if (request.max_artifacts > 500000) {
        diagnostics.push_back("max_artifacts_out_of_range");
    }
    if (request.max_source_inventory_items == 0 ||
        request.max_source_inventory_items > kMaximumMigrationInventoryItems) {
        diagnostics.push_back("max_source_inventory_items_out_of_range");
    }
    if (request.max_target_inventory_items == 0 ||
        request.max_target_inventory_items > kMaximumMigrationInventoryItems) {
        diagnostics.push_back("max_target_inventory_items_out_of_range");
    }
    if (request.max_artifact_bytes > kMaximumMigrationArtifactBytes) {
        diagnostics.push_back("max_artifact_bytes_out_of_range");
    }
    if (request.max_materialization_bytes == 0 ||
        request.max_materialization_bytes > kMaximumMigrationMaterializationBytes) {
        diagnostics.push_back("max_materialization_bytes_out_of_range");
    }
    if (request.max_reference_files == 0 || request.max_reference_files > kMaximumMigrationReferenceFiles) {
        diagnostics.push_back("max_reference_files_out_of_range");
    }
    if (request.max_reference_bytes == 0 || request.max_reference_bytes > kMaximumMigrationReferenceBytes) {
        diagnostics.push_back("max_reference_bytes_out_of_range");
    }
    if (request.max_references == 0 || request.max_references > kMaximumMigrationReferences) {
        diagnostics.push_back("max_references_out_of_range");
    }
    return diagnostics;
}

MigrationPlan MigrationOps::plan(const PlanOptions& options) {
    MigrationPlan plan;
    plan.request = options.request;
    plan.blockers = validate_request(options.request);
    if (!plan.blockers.empty()) {
        sort_unique(plan.blockers);
        plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
        return plan;
    }

    try {
        const auto start = normalized_absolute(options.start_path);
        const auto config_path_opt = ConfigLoader::find_project_config(start);
        if (!config_path_opt) {
            plan.blockers.push_back("project_config_not_found");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        const auto config_path = normalized_absolute(*config_path_opt);
        const auto backlog_root = resolve_backlog_root(options, config_path);
        if (!std::filesystem::exists(backlog_root)) {
            plan.blockers.push_back("backlog_root_not_found");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }

        const auto config_opt = ProjectConfig::load_from_toml(config_path);
        if (!config_opt) {
            plan.blockers.push_back("project_config_invalid");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        const auto& config = *config_opt;
        const auto source_def = config.get_product(options.request.source_product);
        const auto target_def = config.get_product(options.request.target_product);
        if (!source_def) {
            plan.blockers.push_back("source_product_not_registered");
        }
        if (!target_def) {
            plan.blockers.push_back("target_product_not_registered");
        }
        if (!source_def || !target_def) {
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }

        const auto source_root_opt = config.resolve_backlog_root(options.request.source_product, config_path);
        const auto target_root_opt = config.resolve_backlog_root(options.request.target_product, config_path);
        if (!source_root_opt || !std::filesystem::exists(*source_root_opt)) {
            plan.blockers.push_back("source_product_root_not_found");
        }
        if (!target_root_opt || !std::filesystem::exists(*target_root_opt)) {
            plan.blockers.push_back("target_product_root_not_found");
        }
        if (!source_root_opt || !target_root_opt || !std::filesystem::exists(*source_root_opt) || !std::filesystem::exists(*target_root_opt)) {
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        const auto source_root = normalized_absolute(*source_root_opt);
        const auto target_root = normalized_absolute(*target_root_opt);
        if ((!is_within(source_root, backlog_root) && source_root != backlog_root) ||
            (!is_within(target_root, backlog_root) && target_root != backlog_root)) {
            plan.blockers.push_back("product_root_outside_backlog_root");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }

        plan.target_prefix = target_def->prefix;
        if (options.request.expected_target_prefix && *options.request.expected_target_prefix != plan.target_prefix) {
            plan.blockers.push_back("target_prefix_mismatch");
        }
        if (!plan.request.expected_target_prefix) {
            plan.request.expected_target_prefix = plan.target_prefix;
        }
        const auto prefix_collisions = config.find_prefix_collisions(config_path);
        for (const auto& collision : prefix_collisions) {
            if (collision.left_product == options.request.target_product || collision.right_product == options.request.target_product) {
                plan.blockers.push_back("target_prefix_collision");
            }
        }

        CanonicalStore source_store(source_root);
        CanonicalStore target_store(target_root);
        const auto source_inventory = bounded_files_under(
            {source_root / "items"},
            backlog_root,
            options.request.max_source_inventory_items,
            {},
            std::optional<std::string>{".md"});
        const auto target_inventory = bounded_files_under(
            {target_root / "items"},
            backlog_root,
            options.request.max_target_inventory_items,
            {},
            std::optional<std::string>{".md"});
        if (source_inventory.file_limit_exceeded || source_inventory.entry_limit_exceeded) {
            plan.blockers.push_back("source_inventory_limit_exceeded");
        }
        if (target_inventory.file_limit_exceeded || target_inventory.entry_limit_exceeded) {
            plan.blockers.push_back("target_inventory_limit_exceeded");
        }
        if (source_inventory.file_limit_exceeded || source_inventory.entry_limit_exceeded ||
            target_inventory.file_limit_exceeded || target_inventory.entry_limit_exceeded) {
            sort_unique(plan.blockers);
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        std::uintmax_t inventory_bytes = 0;
        bool inventory_io_blocked = false;
        const auto validate_inventory_io = [&](const BoundedFileScan& inventory, const std::string& kind) {
            for (const auto& path : inventory.files) {
                const auto file_bytes = std::filesystem::file_size(path);
                if (file_bytes > kMaximumReferenceFileBytes) {
                    plan.blockers.push_back(
                        kind + "_inventory_file_too_large:" + relative_path(path, backlog_root));
                    inventory_io_blocked = true;
                    continue;
                }
                if (file_bytes > options.request.max_reference_bytes - inventory_bytes) {
                    plan.blockers.push_back(kind + "_inventory_byte_limit_exceeded");
                    inventory_io_blocked = true;
                    return;
                }
                inventory_bytes += file_bytes;
            }
        };
        validate_inventory_io(source_inventory, "source");
        validate_inventory_io(target_inventory, "target");
        if (inventory_io_blocked) {
            sort_unique(plan.blockers);
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        std::vector<BacklogItem> source_items;
        for (const auto& path : source_inventory.files) {
            source_items.push_back(source_store.read_metadata(path));
        }

        std::unordered_map<std::string, std::size_t> source_id_counts;
        std::unordered_map<std::string, std::size_t> source_uid_counts;
        std::unordered_map<std::string, std::string> source_uid_to_id;
        std::unordered_set<std::string> source_ids;
        for (const auto& item : source_items) {
            ++source_id_counts[item.id];
            ++source_uid_counts[item.uid];
            source_uid_to_id.emplace(item.uid, item.id);
            source_ids.insert(item.id);
        }
        for (const auto& [id, count] : source_id_counts) {
            if (count != 1) {
                plan.blockers.push_back("duplicate_source_id:" + id);
            }
        }
        for (const auto& [uid, count] : source_uid_counts) {
            if (uid.empty() || count != 1) {
                plan.blockers.push_back("duplicate_source_uid:" + uid);
            }
        }

        std::unordered_map<std::string, std::string> parent_by_id;
        for (const auto& item : source_items) {
            if (!item.parent) {
                continue;
            }
            std::string parent_id = *item.parent;
            if (const auto uid_it = source_uid_to_id.find(parent_id); uid_it != source_uid_to_id.end()) {
                parent_id = uid_it->second;
            }
            if (!source_ids.contains(parent_id)) {
                plan.blockers.push_back("missing_parent:" + item.id + ":" + *item.parent);
                continue;
            }
            parent_by_id[item.id] = parent_id;
        }
        std::unordered_map<std::string, int> visit_state;
        for (const auto& item : source_items) {
            if (visit_state[item.id] == 2) {
                continue;
            }
            std::vector<std::string> chain;
            std::string current = item.id;
            while (visit_state[current] != 2) {
                if (visit_state[current] == 1) {
                    plan.blockers.push_back("hierarchy_cycle:" + current);
                    break;
                }
                visit_state[current] = 1;
                chain.push_back(current);
                const auto parent = parent_by_id.find(current);
                if (parent == parent_by_id.end()) {
                    break;
                }
                current = parent->second;
            }
            for (const auto& visited : chain) {
                visit_state[visited] = 2;
            }
        }

        std::vector<std::size_t> root_matches;
        for (std::size_t index = 0; index < source_items.size(); ++index) {
            if (source_items[index].id == options.request.source_ref || source_items[index].uid == options.request.source_ref) {
                root_matches.push_back(index);
            }
        }
        if (root_matches.empty()) {
            plan.blockers.push_back("source_root_not_found");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        if (root_matches.size() != 1) {
            plan.blockers.push_back("source_root_ambiguous");
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }

        const auto& root_item = source_items[root_matches.front()];
        plan.source_root_id = root_item.id;
        plan.source_root_uid = root_item.uid;
        std::unordered_set<std::string> selected_ids{root_item.id};
        std::unordered_set<std::string> selected_uids{root_item.uid};
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& item : source_items) {
                if (selected_ids.contains(item.id) || !item.parent) {
                    continue;
                }
                if (selected_ids.contains(*item.parent) || selected_uids.contains(*item.parent)) {
                    selected_ids.insert(item.id);
                    selected_uids.insert(item.uid);
                    changed = true;
                }
            }
        }

        if (root_item.parent) {
            std::string parent_id = *root_item.parent;
            if (const auto uid_it = source_uid_to_id.find(parent_id); uid_it != source_uid_to_id.end()) {
                parent_id = uid_it->second;
            }
            if (!selected_ids.contains(parent_id)) {
                plan.blockers.push_back("source_root_parent_outside_subtree:" + *root_item.parent);
            }
        }

        if (selected_ids.size() > options.request.max_items) {
            plan.blockers.push_back("item_limit_exceeded");
        }

        std::vector<BacklogItem> selected_items;
        for (const auto& item : source_items) {
            if (selected_ids.contains(item.id)) {
                selected_items.push_back(item);
            }
        }
        std::sort(selected_items.begin(), selected_items.end(), [](const auto& left, const auto& right) {
            return left.id < right.id;
        });

        std::set<std::string> target_ids;
        std::set<std::string> target_uids;
        std::unordered_map<std::string, std::size_t> target_id_counts;
        std::unordered_map<std::string, std::size_t> target_uid_counts;
        std::vector<std::filesystem::path> target_snapshot_paths;
        std::vector<BacklogItem> target_items;
        for (const auto& path : target_inventory.files) {
            const auto item = target_store.read_metadata(path);
            target_items.push_back(item);
            target_ids.insert(item.id);
            target_uids.insert(item.uid);
            ++target_id_counts[item.id];
            ++target_uid_counts[item.uid];
            target_snapshot_paths.push_back(path);
        }
        for (const auto& [id, count] : target_id_counts) {
            if (count != 1) {
                plan.blockers.push_back("duplicate_target_id:" + id);
            }
        }
        for (const auto& [uid, count] : target_uid_counts) {
            if (uid.empty() || count != 1) {
                plan.blockers.push_back("duplicate_target_uid:" + uid);
            }
        }

        std::map<ItemType, int> target_max_numbers;
        for (const auto& item : target_items) {
            if (const auto number = display_id_number(item.id, plan.target_prefix, item.type)) {
                target_max_numbers[item.type] = std::max(target_max_numbers[item.type], *number);
            }
        }
        std::map<ItemType, std::size_t> selected_type_counts;
        for (const auto& item : selected_items) {
            ++selected_type_counts[item.type];
        }
        bool target_allocation_limit_exceeded = false;
        for (const auto& [type, count] : selected_type_counts) {
            const auto available = static_cast<std::size_t>(
                std::numeric_limits<int>::max() - target_max_numbers[type]);
            if (count > available) {
                plan.blockers.push_back("target_id_allocation_limit_exceeded:" + type_code(type));
                target_allocation_limit_exceeded = true;
            }
        }
        if (target_allocation_limit_exceeded) {
            sort_unique(plan.blockers);
            plan.status = "blocked";
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }
        std::map<ItemType, std::int64_t> next_numbers;
        std::unordered_map<std::string, std::string> id_mapping;
        std::unordered_set<std::string> selected_source_paths;
        for (const auto& item : selected_items) {
            auto [number_it, inserted] = next_numbers.emplace(item.type, target_max_numbers[item.type] + 1);
            const int number = static_cast<int>(number_it->second++);
            std::ostringstream target_id;
            target_id << plan.target_prefix << '-' << type_code(item.type) << '-' << std::setw(4) << std::setfill('0') << number;
            auto target_item = target_store.create(plan.target_prefix, item.type, item.title, number, std::nullopt);
            target_item.id = target_id.str();

            const auto source_path = item.file_path ? normalized_absolute(*item.file_path) : std::filesystem::path{};
            const auto target_path = target_item.file_path ? normalized_absolute(*target_item.file_path) : std::filesystem::path{};
            if (source_path.empty() || target_path.empty()) {
                plan.blockers.push_back("item_path_resolution_failed:" + item.id);
                continue;
            }
            if (target_ids.contains(target_item.id)) {
                plan.blockers.push_back("target_id_collision:" + target_item.id);
            }
            if (target_uids.contains(item.uid)) {
                plan.blockers.push_back("target_uid_collision:" + item.uid);
            }
            id_mapping[item.id] = target_item.id;
            selected_source_paths.insert(relative_path(source_path, backlog_root));
            plan.items.push_back(MigrationItemMapping{
                item.id,
                target_item.id,
                item.uid,
                item.type,
                relative_path(source_path, backlog_root),
                relative_path(target_path, backlog_root),
            });
        }

        std::vector<std::filesystem::path> source_snapshot_paths;
        for (const auto& mapping : plan.items) {
            source_snapshot_paths.push_back(backlog_root / mapping.source_path);
            plan.affected_paths.push_back(mapping.source_path);
            plan.affected_paths.push_back(mapping.target_path);
        }

        bool artifact_bound_exceeded = false;
        if (options.request.include_owned_artifacts) {
            std::vector<std::filesystem::path> artifact_roots;
            std::unordered_map<std::string, const MigrationItemMapping*> mapping_by_source_id;
            for (const auto& mapping : plan.items) {
                artifact_roots.push_back(source_root / "artifacts" / mapping.source_id);
                mapping_by_source_id.emplace(mapping.source_id, &mapping);
            }
            const auto artifact_inventory = bounded_files_under(
                artifact_roots,
                backlog_root,
                options.request.max_artifacts);
            if (artifact_inventory.file_limit_exceeded || artifact_inventory.entry_limit_exceeded) {
                plan.blockers.push_back("artifact_enumeration_limit_exceeded");
                artifact_bound_exceeded = true;
            }
            std::uintmax_t artifact_bytes = 0;
            for (const auto& artifact_path : artifact_inventory.files) {
                const auto artifact_relative = artifact_path.lexically_relative(source_root / "artifacts");
                if (artifact_relative.empty()) {
                    plan.blockers.push_back("artifact_owner_resolution_failed");
                    continue;
                }
                const auto owner_source_id = (*artifact_relative.begin()).string();
                const auto mapping_it = mapping_by_source_id.find(owner_source_id);
                if (mapping_it == mapping_by_source_id.end()) {
                    plan.blockers.push_back("artifact_owner_resolution_failed:" + owner_source_id);
                    continue;
                }
                const auto& mapping = *mapping_it->second;
                const auto artifact_root = source_root / "artifacts" / mapping.source_id;
                const auto owner_relative = artifact_path.lexically_relative(artifact_root);
                const auto target_path = target_root / "artifacts" / mapping.target_id / owner_relative;
                const auto file_bytes = std::filesystem::file_size(artifact_path);
                if (file_bytes > options.request.max_artifact_bytes - artifact_bytes) {
                    plan.blockers.push_back("artifact_byte_limit_exceeded");
                    artifact_bound_exceeded = true;
                    break;
                }
                StreamingSha256 artifact_hasher;
                const auto read = hash_file_into(
                    artifact_path,
                    options.request.max_artifact_bytes - artifact_bytes,
                    artifact_hasher);
                if (read.limit_exceeded) {
                    plan.blockers.push_back("artifact_byte_limit_exceeded");
                    artifact_bound_exceeded = true;
                    break;
                }
                artifact_bytes += read.bytes;
                plan.artifacts.push_back(MigrationArtifactMapping{
                    mapping.source_id,
                    mapping.target_id,
                    relative_path(artifact_path, backlog_root),
                    relative_path(target_path, backlog_root),
                    artifact_hasher.final_hex(),
                    read.bytes,
                });
                source_snapshot_paths.push_back(artifact_path);
                plan.affected_paths.push_back(relative_path(artifact_path, backlog_root));
                plan.affected_paths.push_back(relative_path(target_path, backlog_root));
            }
        }
        if (artifact_bound_exceeded) {
            sort_unique(plan.affected_paths);
            sort_unique(plan.blockers);
            plan.status = "blocked";
            plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
            return plan;
        }

        const auto products_root = backlog_root / "products";
        const auto reference_files = bounded_files_under(
            {products_root},
            backlog_root,
            options.request.max_reference_files,
            {},
            std::nullopt,
            true);
        if (reference_files.file_limit_exceeded || reference_files.entry_limit_exceeded) {
            plan.blockers.push_back("reference_enumeration_limit_exceeded");
        }
        std::uintmax_t reference_bytes_scanned = 0;
        bool reference_byte_limit_reached = false;
        bool reference_limit_reached = false;
        for (const auto& path : reference_files.files) {
            const auto file_bytes = std::filesystem::file_size(path);
            if (file_bytes > kMaximumReferenceFileBytes) {
                plan.blockers.push_back("reference_scan_file_too_large:" + relative_path(path, backlog_root));
                continue;
            }
            if (file_bytes > options.request.max_reference_bytes - reference_bytes_scanned) {
                plan.blockers.push_back("reference_byte_limit_exceeded");
                reference_byte_limit_reached = true;
                break;
            }
            reference_bytes_scanned += file_bytes;
            const auto content = read_file(path);
            const auto occurrences_by_id = mapped_id_occurrences(content, id_mapping);
            for (const auto& [source_id, occurrences] : occurrences_by_id) {
                const auto rel_string = relative_path(path, backlog_root);
                source_snapshot_paths.push_back(path);
                if (!is_supported_text_reference(path)) {
                    plan.blockers.push_back("unsupported_reference_class:" + rel_string);
                    continue;
                }
                if (plan.references.size() >= options.request.max_references) {
                    plan.blockers.push_back("reference_limit_exceeded");
                    reference_limit_reached = true;
                    break;
                }
                plan.references.push_back(MigrationReferenceRewrite{
                    rel_string,
                    path.extension() == ".md" ? "canonical_markdown_reference" : "bounded_text_reference",
                    source_id,
                    id_mapping.at(source_id),
                    occurrences,
                    !selected_source_paths.contains(rel_string),
                });
                plan.affected_paths.push_back(rel_string);
            }
            if (reference_limit_reached) {
                break;
            }
        }
        if (reference_byte_limit_reached || reference_limit_reached) {
            plan.warnings.push_back("reference_scan_stopped_at_configured_bound");
        }
        std::sort(plan.references.begin(), plan.references.end(), [](const auto& left, const auto& right) {
            return std::tie(left.path, left.source_id, left.target_id) < std::tie(right.path, right.source_id, right.target_id);
        });
        if (std::any_of(plan.references.begin(), plan.references.end(), [](const auto& rewrite) { return rewrite.external_to_subtree; })) {
            plan.warnings.push_back("external_references_require_atomic_rewrite");
        }

        plan.source_revision = snapshot_hash(source_snapshot_paths, backlog_root);
        plan.target_revision = sha256_hex(
            snapshot_hash(target_snapshot_paths, backlog_root) + ":" + sha256_hex(read_file(config_path)));
        if (options.request.expected_source_revision && *options.request.expected_source_revision != plan.source_revision) {
            plan.blockers.push_back("source_revision_mismatch");
        }
        if (!plan.request.expected_source_revision) {
            plan.request.expected_source_revision = plan.source_revision;
        }

        sort_unique(plan.affected_paths);
        if (plan.affected_paths.size() > 500000) {
            plan.blockers.push_back("affected_path_limit_exceeded");
        }
        sort_unique(plan.blockers);
        sort_unique(plan.warnings);
        plan.status = plan.blockers.empty() ? "ready" : "blocked";
        plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
        return plan;
    } catch (const std::exception& error) {
        plan.blockers.push_back("preflight_failed:" + std::string(error.what()));
        sort_unique(plan.blockers);
        plan.status = "blocked";
        plan.plan_hash = sha256_hex(json_string(plan_json(plan, false), false));
        return plan;
    }
}

MigrationResult MigrationOps::apply(const ApplyOptions& options) {
    MigrationResult result;
    result.plan_hash = options.expected_plan_hash;
    result.status = "blocked";
    result.recovery_status = "not_needed";
    if (!options.confirm) {
        result.operation_receipts.push_back("confirm_required");
        return result;
    }
    if (!is_sha256(options.expected_plan_hash)) {
        result.operation_receipts.push_back("invalid_plan_hash");
        return result;
    }

    RecoveryOptions recovery_options{
        .start_path = options.plan.start_path,
        .backlog_root = options.plan.backlog_root,
        .plan_hash = options.expected_plan_hash,
        .confirm = false,
    };

    std::filesystem::path backlog_root;
    std::filesystem::path transaction;
    try {
        backlog_root = resolve_recovery_backlog_root(recovery_options);
        transaction = transaction_root(backlog_root, options.expected_plan_hash);
        if (std::filesystem::is_regular_file(transaction / "journal.json")) {
            const auto existing = load_journal(transaction);
            const auto existing_status = existing["status"].asString();
            if (existing_status == "applied") {
                const auto verification = verify(recovery_options);
                if (verification.status == "verified") {
                    result.status = "already_applied";
                    result.recovery_status = "available";
                    result.idempotent_replay = true;
                    result.operation_receipts.push_back("verified_existing_transaction");
                    return result;
                }
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts.push_back("applied_transaction_failed_verification");
                return result;
            }
            if (existing_status != "rolled_back") {
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts.push_back("incomplete_transaction_requires_rollback");
                return result;
            }
        }

        const auto current_plan = plan(options.plan);
        if (!current_plan.ready()) {
            result.operation_receipts.push_back("plan_not_ready");
            result.operation_receipts.insert(
                result.operation_receipts.end(), current_plan.blockers.begin(), current_plan.blockers.end());
            sort_unique(result.operation_receipts);
            return result;
        }
        if (current_plan.plan_hash != options.expected_plan_hash) {
            result.operation_receipts.push_back("plan_hash_mismatch:" + current_plan.plan_hash);
            return result;
        }

        const auto config_path = ConfigLoader::find_project_config(normalized_absolute(options.plan.start_path));
        if (!config_path) {
            result.operation_receipts.push_back("project_config_not_found");
            return result;
        }
        const auto config = ProjectConfig::load_from_toml(*config_path);
        if (!config) {
            result.operation_receipts.push_back("project_config_invalid");
            return result;
        }
        const auto target_root_opt = config->resolve_backlog_root(current_plan.request.target_product, *config_path);
        const auto source_root_opt = config->resolve_backlog_root(current_plan.request.source_product, *config_path);
        if (!target_root_opt || !source_root_opt) {
            result.operation_receipts.push_back("source_or_target_product_root_not_found");
            return result;
        }
        const auto target_root = normalized_absolute(*target_root_opt);
        const auto source_root = normalized_absolute(*source_root_opt);

        std::map<std::string, MigrationFileOperation> pending;
        std::uintmax_t materialization_bytes = 0;
        const auto ensure_materialization_capacity = [&](std::uintmax_t bytes) {
            if (bytes > current_plan.request.max_materialization_bytes - materialization_bytes) {
                throw std::runtime_error("materialization_byte_limit_exceeded");
            }
        };
        const auto add_pending = [&](const std::string& path, const std::string& kind, std::optional<std::string> content) {
            if (path.empty() || path == "." || path.starts_with("../") || std::filesystem::path(path).is_absolute()) {
                throw std::runtime_error("unsafe_operation_path:" + path);
            }
            if (const auto existing = pending.find(path); existing != pending.end()) {
                if (existing->second.after_content != content) {
                    throw std::runtime_error("conflicting_operation:" + path);
                }
                return;
            }
            const auto content_bytes = content ? static_cast<std::uintmax_t>(content->size()) : 0;
            ensure_materialization_capacity(content_bytes);
            auto [it, inserted] = pending.emplace(path, MigrationFileOperation{});
            it->second.path = path;
            it->second.kind = kind;
            it->second.after_content = std::move(content);
            materialization_bytes += content_bytes;
        };

        std::unordered_set<std::string> retired_paths;
        std::unordered_map<std::string, std::string> migration_id_mapping;
        for (const auto& mapping : current_plan.items) {
            migration_id_mapping[mapping.source_id] = mapping.target_id;
        }
        for (const auto& mapping : current_plan.items) {
            ensure_materialization_capacity(std::filesystem::file_size(backlog_root / mapping.source_path));
            const auto source_content = read_file(backlog_root / mapping.source_path);
            add_pending(mapping.target_path, "target_item", replace_migration_ids(source_content, migration_id_mapping));
            add_pending(mapping.source_path, "retired_source_item", std::nullopt);
            retired_paths.insert(mapping.source_path);
        }
        for (const auto& mapping : current_plan.artifacts) {
            ensure_materialization_capacity(mapping.size_bytes);
            auto content = read_file(backlog_root / mapping.source_path);
            if (is_supported_text_reference(mapping.source_path)) {
                content = replace_migration_ids(std::move(content), migration_id_mapping);
            }
            add_pending(mapping.target_path, "target_artifact", std::move(content));
            add_pending(mapping.source_path, "retired_source_artifact", std::nullopt);
            retired_paths.insert(mapping.source_path);
        }

        std::unordered_set<std::string> rewritten_reference_paths;
        for (const auto& rewrite : current_plan.references) {
            if (retired_paths.contains(rewrite.path)) {
                continue;
            }
            if (rewritten_reference_paths.insert(rewrite.path).second) {
                ensure_materialization_capacity(std::filesystem::file_size(backlog_root / rewrite.path));
                add_pending(
                    rewrite.path,
                    "external_reference",
                    replace_migration_ids(read_file(backlog_root / rewrite.path), migration_id_mapping));
            }
        }

        Json::Value aliases(Json::objectValue);
        aliases["schema"] = "kob.cross_product_migration.aliases.v1";
        aliases["plan_hash"] = current_plan.plan_hash;
        aliases["source_product"] = current_plan.request.source_product;
        aliases["target_product"] = current_plan.request.target_product;
        aliases["source_revision"] = current_plan.source_revision;
        aliases["target_revision"] = current_plan.target_revision;
        Json::Value alias_mappings(Json::arrayValue);
        for (const auto& mapping : current_plan.items) {
            Json::Value value(Json::objectValue);
            value["source_id"] = mapping.source_id;
            value["target_id"] = mapping.target_id;
            value["uid"] = mapping.uid;
            alias_mappings.append(value);
        }
        aliases["mappings"] = alias_mappings;
        const auto alias_path = target_root / "_meta" / "migrations" / (current_plan.plan_hash + ".json");
        add_pending(relative_path(alias_path, backlog_root), "alias_metadata", json_string(aliases, true));

        const auto index_path = backlog_root / ".cache" / "index" / "backlog.db";
        if (std::filesystem::exists(index_path.string() + "-wal") ||
            std::filesystem::exists(index_path.string() + "-shm")) {
            throw std::runtime_error("backlog_index_busy");
        }
        std::filesystem::create_directories(transaction);
        const auto staged_index = transaction / "index-prebuild.db";
        std::error_code index_cleanup_error;
        std::filesystem::remove(staged_index, index_cleanup_error);
        const bool index_existed = std::filesystem::is_regular_file(index_path);
        if (index_existed) {
            ensure_materialization_capacity(std::filesystem::file_size(index_path));
            std::filesystem::copy_file(index_path, staged_index, std::filesystem::copy_options::overwrite_existing);
        }
        {
            BacklogIndex index(staged_index);
            index.initialize();
            CanonicalStore source_store(source_root);
            std::unordered_map<std::string, std::string> mapped_ids;
            for (const auto& mapping : current_plan.items) {
                mapped_ids[mapping.source_id] = mapping.target_id;
            }
            if (!index_existed) {
                std::uintmax_t index_inventory_bytes = 0;
                for (const auto& product_entry : config->products) {
                    const auto& product_name = product_entry.first;
                    const auto product_root = config->resolve_backlog_root(product_name, *config_path);
                    if (!product_root || !std::filesystem::exists(*product_root)) {
                        throw std::runtime_error("registered_product_root_missing_during_index_build:" + product_name);
                    }
                    CanonicalStore product_store(*product_root);
                    const auto inventory_limit = product_name == current_plan.request.source_product
                        ? current_plan.request.max_source_inventory_items
                        : (product_name == current_plan.request.target_product
                            ? current_plan.request.max_target_inventory_items
                            : std::max(
                                current_plan.request.max_source_inventory_items,
                                current_plan.request.max_target_inventory_items));
                    const auto product_inventory = bounded_files_under(
                        {*product_root / "items"},
                        backlog_root,
                        inventory_limit,
                        {},
                        std::optional<std::string>{".md"});
                    if (product_inventory.file_limit_exceeded || product_inventory.entry_limit_exceeded) {
                        throw std::runtime_error("index_inventory_limit_exceeded:" + product_name);
                    }
                    for (const auto& item_path : product_inventory.files) {
                        const auto file_bytes = std::filesystem::file_size(item_path);
                        if (file_bytes > kMaximumReferenceFileBytes) {
                            throw std::runtime_error(
                                "index_inventory_file_too_large:" + product_name + ':' +
                                relative_path(item_path, backlog_root));
                        }
                        if (file_bytes > current_plan.request.max_reference_bytes - index_inventory_bytes) {
                            throw std::runtime_error("index_inventory_byte_limit_exceeded");
                        }
                        index_inventory_bytes += file_bytes;
                        auto item = product_store.read(item_path);
                        if (product_name == current_plan.request.source_product && mapped_ids.contains(item.id)) {
                            continue;
                        }
                        index.index_item(item);
                    }
                }
            }
            for (const auto& mapping : current_plan.items) {
                auto item = source_store.read(backlog_root / mapping.source_path);
                index.remove_item(mapping.source_id);
                item.id = mapping.target_id;
                item.file_path = backlog_root / mapping.target_path;
                if (item.duplicate_of) {
                    if (const auto mapped = mapped_ids.find(*item.duplicate_of); mapped != mapped_ids.end()) {
                        item.duplicate_of = mapped->second;
                    }
                }
                index.index_item(item);
                const auto separator = mapping.target_id.rfind('-');
                if (separator == std::string::npos) {
                    throw std::runtime_error("invalid_target_id:" + mapping.target_id);
                }
                index.ensure_sequence_at_least(
                    current_plan.target_prefix,
                    type_code(mapping.type),
                    std::stoi(mapping.target_id.substr(separator + 1)));
            }
        }
        const auto staged_index_bytes = std::filesystem::file_size(staged_index);
        try {
            ensure_materialization_capacity(staged_index_bytes);
        } catch (...) {
            std::filesystem::remove(staged_index, index_cleanup_error);
            throw;
        }
        auto staged_index_content = read_file(staged_index);
        std::filesystem::remove(staged_index, index_cleanup_error);
        add_pending(
            relative_path(index_path, backlog_root),
            "derived_index",
            std::move(staged_index_content));

        std::error_code cleanup_error;
        std::filesystem::remove_all(transaction / "backup", cleanup_error);
        cleanup_error.clear();
        std::filesystem::remove_all(transaction / "stage", cleanup_error);
        std::filesystem::create_directories(transaction / "backup");
        std::filesystem::create_directories(transaction / "stage");

        std::vector<MigrationFileOperation> operations;
        operations.reserve(pending.size());
        std::size_t operation_index = 0;
        for (auto& [path, operation] : pending) {
            const auto absolute_path = backlog_root / path;
            if (std::filesystem::exists(absolute_path) && !std::filesystem::is_regular_file(absolute_path)) {
                throw std::runtime_error("operation_path_not_regular_file:" + path);
            }
            operation.before_exists = std::filesystem::is_regular_file(absolute_path);
            if ((operation.kind == "target_item" ||
                 operation.kind == "target_artifact" ||
                 operation.kind == "alias_metadata") && operation.before_exists) {
                throw std::runtime_error("target_path_collision:" + path);
            }
            if ((operation.kind == "retired_source_item" ||
                 operation.kind == "retired_source_artifact" ||
                 operation.kind == "external_reference") && !operation.before_exists) {
                throw std::runtime_error("required_source_path_missing:" + path);
            }
            if (operation.before_exists) {
                const auto content = read_file(absolute_path);
                operation.before_sha256 = sha256_hex(content);
                std::ostringstream backup_name;
                backup_name << "backup/" << std::setw(8) << std::setfill('0') << operation_index << ".bin";
                operation.backup_path = backup_name.str();
                write_file_atomic(transaction / operation.backup_path, content);
            }
            operation.after_exists = operation.after_content.has_value();
            if (operation.after_content) {
                operation.after_sha256 = sha256_hex(*operation.after_content);
                std::ostringstream stage_name;
                stage_name << "stage/" << std::setw(8) << std::setfill('0') << operation_index << ".bin";
                operation.stage_path = stage_name.str();
                write_file_atomic(transaction / operation.stage_path, *operation.after_content);
                if (!file_matches(transaction / operation.stage_path, true, operation.after_sha256)) {
                    throw std::runtime_error("stage_hash_mismatch:" + path);
                }
            }
            operations.push_back(std::move(operation));
            ++operation_index;
        }

        Json::Value journal(Json::objectValue);
        journal["schema"] = "kob.cross_product_migration.journal.v1";
        journal["status"] = "prepared";
        journal["plan_hash"] = current_plan.plan_hash;
        journal["plan"] = parse_json(current_plan.to_json(false));
        Json::Value operation_values(Json::arrayValue);
        for (const auto& operation : operations) {
            operation_values.append(operation_json(operation));
        }
        journal["operations"] = operation_values;
        write_journal(transaction, journal);

        const auto inject = [&](const std::string& phase) {
            if (options.inject_failure_after && *options.inject_failure_after == phase) {
                throw std::runtime_error("injected_failure:" + phase);
            }
        };
        inject("after_stage");

        for (const auto& operation : operations) {
            if (!file_matches(backlog_root / operation.path, operation.before_exists, operation.before_sha256)) {
                throw std::runtime_error("concurrent_drift_before_apply:" + operation.path);
            }
        }

        const auto publish_kind = [&](const std::string& kind) {
            for (const auto& operation : operations) {
                if (operation.kind != kind || !operation.after_exists) {
                    continue;
                }
                if (!file_matches(backlog_root / operation.path, operation.before_exists, operation.before_sha256)) {
                    throw std::runtime_error("concurrent_drift_before_publish:" + operation.path);
                }
                write_file_atomic(backlog_root / operation.path, read_file(transaction / operation.stage_path));
            }
        };
        publish_kind("target_item");
        publish_kind("target_artifact");
        publish_kind("alias_metadata");
        inject("after_target_publish");

        publish_kind("external_reference");
        inject("after_reference_rewrite");

        for (const auto& operation : operations) {
            if (operation.after_exists) {
                continue;
            }
            if (!file_matches(backlog_root / operation.path, operation.before_exists, operation.before_sha256)) {
                throw std::runtime_error("concurrent_drift_before_retire:" + operation.path);
            }
            std::error_code remove_error;
            std::filesystem::remove(backlog_root / operation.path, remove_error);
            if (remove_error) {
                throw std::runtime_error("source_retire_failed:" + operation.path + ":" + remove_error.message());
            }
        }
        publish_kind("derived_index");
        inject("after_source_retire");

        for (const auto& operation : operations) {
            if (!file_matches(backlog_root / operation.path, operation.after_exists, operation.after_sha256)) {
                throw std::runtime_error("post_write_hash_mismatch:" + operation.path);
            }
            result.changed_paths.push_back(operation.path);
        }
        sort_unique(result.changed_paths);
        journal["status"] = "applied";
        write_journal(transaction, journal);

        const auto verification = verify(recovery_options);
        if (verification.status != "verified") {
            throw std::runtime_error("postcondition_verification_failed");
        }
        result.status = "applied";
        result.recovery_status = "available";
        result.operation_receipts = {
            "staged_outputs_verified",
            "target_subtree_published",
            "external_references_rewritten",
            "source_subtree_retired",
            "postconditions_verified",
        };
        return result;
    } catch (const std::exception& error) {
        result.operation_receipts.push_back(error.what());
        if (!transaction.empty() && std::filesystem::is_regular_file(transaction / "journal.json")) {
            try {
                auto journal = load_journal(transaction);
                auto operations = journal_operations(journal);
                std::vector<std::string> failures;
                const auto restored = restore_before_state(backlog_root, transaction, operations, failures);
                result.changed_paths = restored;
                if (failures.empty()) {
                    journal["status"] = "rolled_back";
                    journal["last_error"] = error.what();
                    write_journal(transaction, journal);
                    result.status = "rolled_back";
                    result.recovery_status = "completed";
                    result.operation_receipts.push_back("automatic_rollback_completed");
                } else {
                    journal["status"] = "recovery_required";
                    journal["last_error"] = error.what();
                    write_journal(transaction, journal);
                    result.status = "recovery_required";
                    result.recovery_status = "required";
                    result.operation_receipts.insert(
                        result.operation_receipts.end(), failures.begin(), failures.end());
                }
            } catch (const std::exception& recovery_error) {
                result.status = "recovery_required";
                result.recovery_status = "required";
                result.operation_receipts.push_back("automatic_rollback_failed:" + std::string(recovery_error.what()));
            }
        }
        sort_unique(result.operation_receipts);
        return result;
    }
}

MigrationVerification MigrationOps::verify(const RecoveryOptions& options) {
    MigrationVerification verification;
    verification.plan_hash = options.plan_hash;
    verification.status = "not_applied";
    try {
        const auto backlog_root = resolve_recovery_backlog_root(options);
        const auto transaction = transaction_root(backlog_root, options.plan_hash);
        const auto journal = load_journal(transaction);
        if (journal["plan_hash"].asString() != options.plan_hash) {
            verification.failures.push_back("journal_plan_hash_mismatch");
            verification.status = "failed";
            return verification;
        }
        if (journal["status"].asString() != "applied") {
            verification.failures.push_back("migration_not_applied:" + journal["status"].asString());
            return verification;
        }

        const auto operations = journal_operations(journal);
        for (const auto& operation : operations) {
            if (!file_matches(backlog_root / operation.path, operation.after_exists, operation.after_sha256)) {
                verification.failures.push_back("operation_postcondition_failed:" + operation.path);
            }
        }
        if (verification.failures.empty()) {
            verification.postconditions.push_back("journal_file_hashes_match");
        }

        const auto& plan_value = journal["plan"];
        auto hashed_plan_value = plan_value;
        const auto embedded_plan_hash = hashed_plan_value["plan_hash"].asString();
        hashed_plan_value.removeMember("plan_hash");
        if (embedded_plan_hash != options.plan_hash ||
            sha256_hex(json_string(hashed_plan_value, false)) != options.plan_hash) {
            verification.failures.push_back("journal_embedded_plan_hash_mismatch");
            verification.status = "failed";
            return verification;
        }
        const auto& request_value = plan_value["request"];
        const bool reference_bounds_valid =
            request_value["max_reference_files"].isUInt64() &&
            request_value["max_reference_bytes"].isUInt64() &&
            request_value["max_references"].isUInt64() &&
            request_value["max_reference_files"].asUInt64() > 0 &&
            request_value["max_reference_files"].asUInt64() <= kMaximumMigrationReferenceFiles &&
            request_value["max_reference_bytes"].asUInt64() > 0 &&
            request_value["max_reference_bytes"].asUInt64() <= kMaximumMigrationReferenceBytes &&
            request_value["max_references"].asUInt64() > 0 &&
            request_value["max_references"].asUInt64() <= kMaximumMigrationReferences;
        if (!reference_bounds_valid) {
            verification.failures.push_back("journal_reference_bounds_invalid");
        }
        const auto config_path = ConfigLoader::find_project_config(normalized_absolute(options.start_path));
        if (!config_path) {
            verification.failures.push_back("project_config_not_found");
        } else if (const auto config = ProjectConfig::load_from_toml(*config_path)) {
            const auto target_product = plan_value["request"]["target_product"].asString();
            const auto target_root = config->resolve_backlog_root(target_product, *config_path);
            if (!target_root) {
                verification.failures.push_back("target_product_root_not_found");
            } else {
                CanonicalStore target_store(*target_root);
                for (const auto& item_value : plan_value["items"]) {
                    const auto source_path = backlog_root / item_value["source_path"].asString();
                    const auto target_path = backlog_root / item_value["target_path"].asString();
                    if (std::filesystem::exists(source_path)) {
                        verification.failures.push_back("source_item_still_active:" + item_value["source_id"].asString());
                    }
                    try {
                        const auto item = target_store.read(target_path);
                        if (item.id != item_value["target_id"].asString()) {
                            verification.failures.push_back("target_id_mismatch:" + item_value["target_id"].asString());
                        }
                        if (item.uid != item_value["uid"].asString()) {
                            verification.failures.push_back("target_uid_mismatch:" + item_value["target_id"].asString());
                        }
                    } catch (const std::exception& error) {
                        verification.failures.push_back(
                            "target_item_invalid:" + item_value["target_id"].asString() + ":" + error.what());
                    }
                }
            }
        } else {
            verification.failures.push_back("project_config_invalid");
        }

        std::unordered_set<std::string> alias_paths;
        for (const auto& operation : operations) {
            if (operation.kind == "alias_metadata") {
                alias_paths.insert(operation.path);
            }
        }
        if (alias_paths.size() != 1) {
            verification.failures.push_back("alias_metadata_missing_or_ambiguous");
        } else {
            verification.postconditions.push_back("old_ids_resolve_through_migration_metadata");
        }

        const auto index_path = backlog_root / ".cache" / "index" / "backlog.db";
        if (!std::filesystem::is_regular_file(index_path)) {
            verification.failures.push_back("derived_index_missing");
        } else {
            BacklogIndex index(index_path);
            for (const auto& item_value : plan_value["items"]) {
                const auto source_id = item_value["source_id"].asString();
                const auto target_id = item_value["target_id"].asString();
                const auto uid = item_value["uid"].asString();
                const auto target_path = normalized_absolute(backlog_root / item_value["target_path"].asString());
                if (index.get_path_by_id(source_id)) {
                    verification.failures.push_back("source_id_still_indexed:" + source_id);
                }
                const auto indexed_target = index.get_path_by_id(target_id);
                const auto indexed_uid = index.get_path_by_uid(uid);
                if (!indexed_target || normalized_absolute(*indexed_target) != target_path) {
                    verification.failures.push_back("target_id_index_mismatch:" + target_id);
                }
                if (!indexed_uid || normalized_absolute(*indexed_uid) != target_path) {
                    verification.failures.push_back("target_uid_index_mismatch:" + uid);
                }
            }
            if (std::none_of(verification.failures.begin(), verification.failures.end(), [](const auto& failure) {
                    return failure.find("index") != std::string::npos;
                })) {
                verification.postconditions.push_back("derived_index_rewritten");
            }
        }

        const auto products_root = backlog_root / "products";
        std::unordered_map<std::string, std::string> verification_id_mapping;
        for (const auto& item_value : plan_value["items"]) {
            verification_id_mapping[item_value["source_id"].asString()] = item_value["target_id"].asString();
        }
        if (reference_bounds_valid) {
            const auto max_reference_files = static_cast<std::size_t>(request_value["max_reference_files"].asUInt64());
            const auto max_reference_bytes = static_cast<std::uintmax_t>(request_value["max_reference_bytes"].asUInt64());
            const auto max_references = static_cast<std::size_t>(request_value["max_references"].asUInt64());
            if (plan_value["references"].size() > max_references) {
                verification.failures.push_back("verification_persisted_reference_limit_exceeded");
            }
            const auto reference_files = bounded_files_under(
                {products_root},
                backlog_root,
                max_reference_files,
                alias_paths,
                std::nullopt,
                true);
            if (reference_files.file_limit_exceeded || reference_files.entry_limit_exceeded) {
                verification.failures.push_back("verification_reference_enumeration_limit_exceeded");
            }
            std::uintmax_t reference_bytes_scanned = 0;
            std::size_t references_scanned = 0;
            bool reference_limit_reached = false;
            for (const auto& path : reference_files.files) {
                const auto rel = relative_path(path, backlog_root);
                const auto file_bytes = std::filesystem::file_size(path);
                if (file_bytes > kMaximumReferenceFileBytes) {
                    verification.failures.push_back("verification_scan_file_too_large:" + rel);
                    continue;
                }
                if (file_bytes > max_reference_bytes - reference_bytes_scanned) {
                    verification.failures.push_back("verification_reference_byte_limit_exceeded");
                    break;
                }
                reference_bytes_scanned += file_bytes;
                const auto content = read_file(path);
                const auto occurrences = mapped_id_occurrences(content, verification_id_mapping);
                if (!occurrences.empty() && !is_supported_text_reference(path)) {
                    verification.failures.push_back("verification_unsupported_reference_class:" + rel);
                }
                for (const auto& occurrence : occurrences) {
                    if (references_scanned >= max_references) {
                        verification.failures.push_back("verification_reference_limit_exceeded");
                        reference_limit_reached = true;
                        break;
                    }
                    ++references_scanned;
                    verification.failures.push_back("unresolved_old_id:" + rel + ":" + occurrence.first);
                }
                if (reference_limit_reached) {
                    break;
                }
            }
        }
        if (reference_bounds_valid &&
            std::none_of(verification.failures.begin(), verification.failures.end(), [](const auto& failure) {
                return failure.starts_with("unresolved_old_id:") ||
                       failure.starts_with("verification_reference_") ||
                       failure.starts_with("verification_scan_file_too_large:") ||
                       failure.starts_with("verification_unsupported_reference_class:");
            })) {
            verification.postconditions.push_back("canonical_references_rewritten");
        }

        sort_unique(verification.postconditions);
        sort_unique(verification.failures);
        verification.status = verification.failures.empty() ? "verified" : "failed";
        if (verification.status == "verified") {
            verification.postconditions.push_back("target_uids_and_content_hashes_verified");
            sort_unique(verification.postconditions);
        }
        return verification;
    } catch (const std::exception& error) {
        verification.failures.push_back(error.what());
        verification.status = error.what() == std::string("migration_journal_not_found") ? "not_applied" : "failed";
        return verification;
    }
}

MigrationStatus MigrationOps::status(const RecoveryOptions& options) {
    MigrationStatus result;
    result.plan_hash = options.plan_hash;
    result.status = "unknown";
    result.recovery_status = "not_needed";
    try {
        const auto backlog_root = resolve_recovery_backlog_root(options);
        const auto journal = load_journal(transaction_root(backlog_root, options.plan_hash));
        const auto journal_status = journal["status"].asString();
        if (journal_status == "prepared" || journal_status == "applying" || journal_status == "recovery_required") {
            result.status = "recovery_required";
            result.recovery_status = "required";
            result.rollback_supported = true;
        } else if (journal_status == "applied") {
            result.status = "applied";
            result.recovery_status = "available";
            result.rollback_supported = true;
        } else if (journal_status == "rolled_back") {
            result.status = "rolled_back";
            result.recovery_status = "completed";
        }
        return result;
    } catch (const std::exception&) {
        return result;
    }
}

MigrationRollback MigrationOps::rollback(const RecoveryOptions& options) {
    MigrationRollback result;
    result.plan_hash = options.plan_hash;
    result.status = "failed";
    if (!options.confirm) {
        result.failures.push_back("confirm_required");
        return result;
    }
    try {
        const auto backlog_root = resolve_recovery_backlog_root(options);
        const auto transaction = transaction_root(backlog_root, options.plan_hash);
        auto journal = load_journal(transaction);
        if (journal["status"].asString() == "rolled_back") {
            result.status = "not_needed";
            return result;
        }
        const auto operations = journal_operations(journal);
        result.restored_paths = restore_before_state(
            backlog_root, transaction, operations, result.failures);
        if (result.failures.empty()) {
            journal["status"] = "rolled_back";
            write_journal(transaction, journal);
            result.status = "rolled_back";
        }
        return result;
    } catch (const std::exception& error) {
        result.failures.push_back(error.what());
        return result;
    }
}

} // namespace kano::backlog_ops
