#include "kano/backlog_ops/migration/migration_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"

#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
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

std::string sha256_hex(const std::string& value) {
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

    std::vector<uint8_t> data(value.begin(), value.end());
    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8u;
    data.push_back(0x80u);
    while ((data.size() % 64u) != 56u) {
        data.push_back(0u);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<uint8_t>((bit_len >> shift) & 0xffu));
    }

    std::array<uint32_t, 8> hash = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    for (std::size_t offset = 0; offset < data.size(); offset += 64u) {
        std::array<uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16u; ++index) {
            const std::size_t base = offset + index * 4u;
            words[index] = (static_cast<uint32_t>(data[base]) << 24u) |
                           (static_cast<uint32_t>(data[base + 1u]) << 16u) |
                           (static_cast<uint32_t>(data[base + 2u]) << 8u) |
                           static_cast<uint32_t>(data[base + 3u]);
        }
        for (std::size_t index = 16u; index < 64u; ++index) {
            const uint32_t s0 = sha256_rotr(words[index - 15u], 7u) ^ sha256_rotr(words[index - 15u], 18u) ^ (words[index - 15u] >> 3u);
            const uint32_t s1 = sha256_rotr(words[index - 2u], 17u) ^ sha256_rotr(words[index - 2u], 19u) ^ (words[index - 2u] >> 10u);
            words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
        }

        uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
        uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
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
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto part : hash) {
        output << std::setw(8) << part;
    }
    return output.str();
}

std::string snapshot_hash(std::vector<std::filesystem::path> paths, const std::filesystem::path& root) {
    std::sort(paths.begin(), paths.end(), [&](const auto& left, const auto& right) {
        return relative_path(left, root) < relative_path(right, root);
    });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::ostringstream canonical;
    for (const auto& path : paths) {
        if (!std::filesystem::is_regular_file(path)) {
            continue;
        }
        const auto rel = relative_path(path, root);
        const auto content = read_file(path);
        canonical << rel.size() << ':' << rel << ':' << content.size() << ':' << content;
    }
    return sha256_hex(canonical.str());
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

std::size_t count_occurrences(const std::string& content, const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = content.find(value, offset)) != std::string::npos) {
        ++count;
        offset += value.size();
    }
    return count;
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

std::vector<std::filesystem::path> regular_files_under(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
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
    return diagnostics;
}

MigrationPlan MigrationOps::plan(const PlanOptions& options) {
    MigrationPlan plan;
    plan.request = options.request;
    plan.blockers = validate_request(options.request);

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
        const auto prefix_collisions = config.find_prefix_collisions(config_path);
        for (const auto& collision : prefix_collisions) {
            if (collision.left_product == options.request.target_product || collision.right_product == options.request.target_product) {
                plan.blockers.push_back("target_prefix_collision");
            }
        }

        CanonicalStore source_store(source_root);
        CanonicalStore target_store(target_root);
        std::vector<BacklogItem> source_items;
        std::vector<std::filesystem::path> source_item_paths;
        for (const auto& path : source_store.list_items()) {
            source_items.push_back(source_store.read_metadata(path));
            source_item_paths.push_back(path);
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
        std::vector<std::filesystem::path> target_snapshot_paths;
        for (const auto& path : target_store.list_items()) {
            const auto item = target_store.read_metadata(path);
            target_ids.insert(item.id);
            target_uids.insert(item.uid);
            target_snapshot_paths.push_back(path);
        }

        std::map<ItemType, int> next_numbers;
        std::unordered_map<std::string, std::string> id_mapping;
        std::unordered_set<std::string> selected_source_paths;
        for (const auto& item : selected_items) {
            auto [number_it, inserted] = next_numbers.emplace(item.type, target_store.get_max_id_number(plan.target_prefix, item.type) + 1);
            const int number = number_it->second++;
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

        if (options.request.include_owned_artifacts) {
            for (const auto& mapping : plan.items) {
                const auto artifact_root = source_root / "artifacts" / mapping.source_id;
                for (const auto& artifact_path : regular_files_under(artifact_root)) {
                    if (plan.artifacts.size() >= options.request.max_artifacts) {
                        plan.blockers.push_back("artifact_limit_exceeded");
                        break;
                    }
                    const auto owner_relative = artifact_path.lexically_relative(artifact_root);
                    const auto target_path = target_root / "artifacts" / mapping.target_id / owner_relative;
                    const auto content = read_file(artifact_path);
                    plan.artifacts.push_back(MigrationArtifactMapping{
                        mapping.source_id,
                        mapping.target_id,
                        relative_path(artifact_path, backlog_root),
                        relative_path(target_path, backlog_root),
                        sha256_hex(content),
                        static_cast<std::uintmax_t>(content.size()),
                    });
                    source_snapshot_paths.push_back(artifact_path);
                    plan.affected_paths.push_back(relative_path(artifact_path, backlog_root));
                    plan.affected_paths.push_back(relative_path(target_path, backlog_root));
                }
            }
        }

        const auto products_root = backlog_root / "products";
        for (const auto& path : regular_files_under(products_root)) {
            const auto rel = std::filesystem::relative(path, backlog_root);
            if (is_derived_or_internal_path(rel)) {
                continue;
            }
            constexpr std::uintmax_t kMaxReferenceScanBytes = 16u * 1024u * 1024u;
            if (std::filesystem::file_size(path) > kMaxReferenceScanBytes) {
                plan.blockers.push_back("reference_scan_file_too_large:" + relative_path(path, backlog_root));
                continue;
            }
            const auto content = read_file(path);
            for (const auto& mapping : plan.items) {
                const auto occurrences = count_occurrences(content, mapping.source_id);
                if (occurrences == 0) {
                    continue;
                }
                const auto rel_string = relative_path(path, backlog_root);
                source_snapshot_paths.push_back(path);
                if (!is_supported_text_reference(path)) {
                    plan.blockers.push_back("unsupported_reference_class:" + rel_string);
                    continue;
                }
                plan.references.push_back(MigrationReferenceRewrite{
                    rel_string,
                    path.extension() == ".md" ? "canonical_markdown_reference" : "bounded_text_reference",
                    mapping.source_id,
                    mapping.target_id,
                    occurrences,
                    !selected_source_paths.contains(rel_string),
                });
                plan.affected_paths.push_back(rel_string);
            }
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

        sort_unique(plan.affected_paths);
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

} // namespace kano::backlog_ops
