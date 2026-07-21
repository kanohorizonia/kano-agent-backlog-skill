#include "kano/backlog_ops/relation/relation_ops.hpp"

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/refs/ref_parser.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <variant>

namespace kano::backlog_ops {
namespace {

using kano::backlog_core::BacklogContext;
using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ConfigLoader;
using kano::backlog_core::DisplayIdRef;
using kano::backlog_core::ProjectConfig;
using kano::backlog_core::RefParser;
using kano::backlog_core::RefResolver;
using kano::backlog_core::StateMachine;
using kano::backlog_core::UuidRef;

struct ProductRuntime {
    std::string name;
    std::string prefix;
    BacklogContext context;
};

struct Catalog {
    std::filesystem::path config_path;
    std::vector<ProductRuntime> products;
};

struct ScanResult {
    std::vector<RelationEntry> relations;
    std::size_t items_scanned = 0;
    std::size_t unresolved_links = 0;
};

struct ItemReadTask {
    const ProductRuntime* product = nullptr;
    std::filesystem::path path;
};

struct ScannedMetadata {
    const ProductRuntime* product = nullptr;
    BacklogItem item;
    bool valid = false;
};

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim_copy(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void require_safe_identifier(const std::string& value, const std::string& field) {
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty()) {
        throw std::runtime_error(field + " is required");
    }
    if (trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos ||
        trimmed.find(':') != std::string::npos || trimmed == "." || trimmed == ".." ||
        trimmed.rfind(".", 0) == 0) {
        throw std::runtime_error(field + " must be a product alias, display ID, or UID, not a path-like value");
    }
}

void require_safe_item_ref(const std::string& value, const std::string& field) {
    require_safe_identifier(value, field);
    const auto parsed = RefParser::parse(value);
    if (!parsed || (!std::holds_alternative<DisplayIdRef>(*parsed) && !std::holds_alternative<UuidRef>(*parsed))) {
        throw std::runtime_error(field + " must be a canonical display ID or UUIDv7 UID");
    }
}

Catalog load_catalog(const std::filesystem::path& start_path, std::size_t max_products) {
    const auto config_path = ConfigLoader::find_project_config(start_path);
    if (!config_path) {
        throw std::runtime_error("Project config required but not found for relation operation");
    }
    const auto config = ProjectConfig::load_from_toml(*config_path);
    if (!config) {
        throw std::runtime_error("Failed to parse project config: " + config_path->string());
    }
    if (const auto collisions = config->find_prefix_collisions(*config_path); !collisions.empty()) {
        throw std::runtime_error(ProjectConfig::describe_prefix_collisions(collisions));
    }
    if (config->products.size() > max_products) {
        throw std::runtime_error("Relation product scan limit exceeded: " +
            std::to_string(config->products.size()) + " > " + std::to_string(max_products));
    }

    Catalog catalog;
    catalog.config_path = *config_path;
    for (const auto& [name, definition] : config->products) {
        ProductRuntime runtime;
        runtime.name = name;
        runtime.prefix = definition.prefix;
        runtime.context = BacklogContext::resolve(*config_path, name, std::nullopt);
        catalog.products.push_back(std::move(runtime));
    }
    return catalog;
}

const ProductRuntime& resolve_product(const Catalog& catalog, const std::string& alias, const std::string& field) {
    require_safe_identifier(alias, field);
    const std::string needle = lower_copy(trim_copy(alias));
    std::vector<const ProductRuntime*> matches;
    for (const auto& product : catalog.products) {
        if (lower_copy(product.name) == needle || lower_copy(product.prefix) == needle) {
            matches.push_back(&product);
        }
    }
    if (matches.empty()) {
        throw std::runtime_error("Unknown " + field + ": " + alias);
    }
    if (matches.size() != 1) {
        throw std::runtime_error("Ambiguous " + field + ": " + alias);
    }
    return *matches.front();
}

const ProductRuntime* product_for_prefix(const Catalog& catalog, const std::string& prefix) {
    const std::string needle = lower_copy(prefix);
    const ProductRuntime* match = nullptr;
    for (const auto& product : catalog.products) {
        if (lower_copy(product.prefix) == needle) {
            if (match != nullptr) {
                return nullptr;
            }
            match = &product;
        }
    }
    return match;
}

RelationEndpoint resolve_endpoint(
    const ProductRuntime& product,
    const std::string& item_ref,
    const std::string& field
) {
    require_safe_item_ref(item_ref, field);
    CanonicalStore store(product.context.product_root);
    RefResolver resolver(store);
    const auto item = resolver.resolve(item_ref);
    return RelationEndpoint{product.name, item.id, item.uid};
}

struct MutationTargetResolution {
    RelationEndpoint endpoint;
    bool exists = true;
};

MutationTargetResolution resolve_mutation_target(
    const ProductRuntime& product,
    const std::string& item_ref,
    bool add
) {
    require_safe_item_ref(item_ref, "target_item");
    const auto parsed = RefParser::parse(item_ref);
    if (!add && parsed && std::holds_alternative<DisplayIdRef>(*parsed)) {
        const auto& display = std::get<DisplayIdRef>(*parsed);
        if (lower_copy(display.product) != lower_copy(product.prefix)) {
            throw std::runtime_error(
                "Missing target display ID prefix does not match target product: " + item_ref +
                " vs " + product.prefix);
        }
        CanonicalStore store(product.context.product_root);
        if (store.find_item_paths_by_id(item_ref).empty()) {
            return MutationTargetResolution{RelationEndpoint{product.name, item_ref, ""}, false};
        }
    }
    return MutationTargetResolution{resolve_endpoint(product, item_ref, "target_item"), true};
}

const ProductRuntime& product_by_name(const Catalog& catalog, const std::string& name) {
    const auto it = std::find_if(catalog.products.begin(), catalog.products.end(), [&](const auto& product) {
        return product.name == name;
    });
    if (it == catalog.products.end()) {
        throw std::runtime_error("Configured product disappeared during relation operation: " + name);
    }
    return *it;
}

RelationEntry make_entry(
    const RelationEndpoint& stored_source,
    const RelationEndpoint& stored_target,
    RelationType stored_type
) {
    RelationEntry entry;
    entry.stored_source = stored_source;
    entry.stored_target = stored_target;
    entry.stored_relation_type = stored_type;
    if (stored_type == RelationType::BlockedBy) {
        entry.source = stored_target;
        entry.target = stored_source;
        entry.relation_type = RelationType::Blocks;
    } else {
        entry.source = stored_source;
        entry.target = stored_target;
        entry.relation_type = stored_type;
    }
    return entry;
}

ScanResult scan_catalog(const Catalog& catalog, std::size_t max_items) {
    ScanResult result;
    std::vector<ItemReadTask> tasks;
    for (const auto& product : catalog.products) {
        CanonicalStore store(product.context.product_root);
        for (const auto& path : store.list_items()) {
            if (tasks.size() >= max_items) {
                throw std::runtime_error("Relation item scan limit exceeded: " + std::to_string(max_items));
            }
            tasks.push_back(ItemReadTask{&product, path});
        }
    }

    std::vector<ScannedMetadata> metadata(tasks.size());
    std::atomic<std::size_t> next_task{0};
    const auto hardware_threads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    const std::size_t worker_count = std::min<std::size_t>(
        tasks.size(), std::min<std::size_t>(16, std::max<std::size_t>(4, hardware_threads)));
    const auto worker = [&]() {
        while (true) {
            const std::size_t index = next_task.fetch_add(1, std::memory_order_relaxed);
            if (index >= tasks.size()) {
                return;
            }
            const auto& task = tasks[index];
            auto& scanned = metadata[index];
            scanned.product = task.product;
            try {
                CanonicalStore store(task.product->context.product_root);
                scanned.item = store.read_metadata(task.path);
                scanned.valid = true;
            } catch (...) {
            }
        }
    };

    std::vector<std::future<void>> workers;
    workers.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers.push_back(std::async(std::launch::async, worker));
    }
    for (auto& active_worker : workers) {
        active_worker.get();
    }

    result.items_scanned = tasks.size();
    using EndpointMap = std::map<std::string, std::vector<RelationEndpoint>>;
    std::map<std::string, EndpointMap> endpoints_by_product_and_id;
    EndpointMap endpoints_by_uid;
    for (const auto& scanned : metadata) {
        if (!scanned.valid) {
            ++result.unresolved_links;
            continue;
        }
        RelationEndpoint endpoint{scanned.product->name, scanned.item.id, scanned.item.uid};
        endpoints_by_product_and_id[scanned.product->name][scanned.item.id].push_back(endpoint);
        endpoints_by_uid[scanned.item.uid].push_back(std::move(endpoint));
    }

    const auto resolve_stored_ref = [&](const std::string& ref) -> std::optional<RelationEndpoint> {
        const auto parsed = RefParser::parse(ref);
        if (!parsed) {
            return std::nullopt;
        }
        const std::vector<RelationEndpoint>* matches = nullptr;
        if (std::holds_alternative<DisplayIdRef>(*parsed)) {
            const auto& display = std::get<DisplayIdRef>(*parsed);
            const auto* product = product_for_prefix(catalog, display.product);
            if (!product) {
                return std::nullopt;
            }
            const auto product_it = endpoints_by_product_and_id.find(product->name);
            if (product_it == endpoints_by_product_and_id.end()) {
                return std::nullopt;
            }
            const auto item_it = product_it->second.find(ref);
            if (item_it != product_it->second.end()) {
                matches = &item_it->second;
            }
        } else if (std::holds_alternative<UuidRef>(*parsed)) {
            const auto item_it = endpoints_by_uid.find(ref);
            if (item_it != endpoints_by_uid.end()) {
                matches = &item_it->second;
            }
        }
        if (!matches || matches->size() != 1) {
            return std::nullopt;
        }
        return matches->front();
    };

    for (const auto& scanned : metadata) {
        if (!scanned.valid) {
            continue;
        }
        const RelationEndpoint source{scanned.product->name, scanned.item.id, scanned.item.uid};
        const auto append = [&](const std::vector<std::string>& refs, RelationType type) {
            for (const auto& ref : refs) {
                auto target = resolve_stored_ref(ref);
                if (!target) {
                    ++result.unresolved_links;
                    continue;
                }
                result.relations.push_back(make_entry(source, *target, type));
            }
        };
        append(scanned.item.links.relates, RelationType::Relates);
        append(scanned.item.links.blocks, RelationType::Blocks);
        append(scanned.item.links.blocked_by, RelationType::BlockedBy);
    }
    return result;
}

std::string endpoint_key(const RelationEndpoint& endpoint) {
    return endpoint.product + ":" + endpoint.uid;
}

bool same_endpoint(const RelationEndpoint& left, const RelationEndpoint& right) {
    return left.product == right.product && left.uid == right.uid;
}

bool equivalent_relation(const RelationEntry& left, const RelationEntry& right) {
    if (left.relation_type == RelationType::Relates && right.relation_type == RelationType::Relates) {
        return (same_endpoint(left.source, right.source) && same_endpoint(left.target, right.target)) ||
            (same_endpoint(left.source, right.target) && same_endpoint(left.target, right.source));
    }
    return left.relation_type == right.relation_type &&
        same_endpoint(left.source, right.source) && same_endpoint(left.target, right.target);
}

RelationEntry requested_entry(
    const RelationEndpoint& source,
    const RelationEndpoint& target,
    RelationType type
) {
    return make_entry(source, target, type);
}

std::vector<RelationEntry> matching_relations(const ScanResult& scan, const RelationEntry& requested) {
    std::vector<RelationEntry> matches;
    for (const auto& relation : scan.relations) {
        if (equivalent_relation(relation, requested)) {
            matches.push_back(relation);
        }
    }
    return matches;
}

std::vector<RelationEndpoint> find_cycle_path(
    const ScanResult& scan,
    const RelationEntry& proposed
) {
    if (proposed.relation_type == RelationType::Relates) {
        return {};
    }
    std::map<std::string, std::vector<RelationEndpoint>> adjacency;
    for (const auto& relation : scan.relations) {
        if (relation.relation_type == RelationType::Blocks) {
            adjacency[endpoint_key(relation.source)].push_back(relation.target);
        }
    }

    const std::string goal = endpoint_key(proposed.source);
    std::set<std::string> visiting;
    std::vector<RelationEndpoint> path{proposed.source, proposed.target};
    std::function<bool(const RelationEndpoint&)> visit = [&](const RelationEndpoint& current) {
        const std::string key = endpoint_key(current);
        if (key == goal) {
            return true;
        }
        if (!visiting.insert(key).second) {
            return false;
        }
        for (const auto& next : adjacency[key]) {
            path.push_back(next);
            if (visit(next)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    };
    if (visit(proposed.target)) {
        return path;
    }
    return {};
}

std::vector<std::string>& relation_values(BacklogItem& item, RelationType type) {
    switch (type) {
        case RelationType::Relates: return item.links.relates;
        case RelationType::Blocks: return item.links.blocks;
        case RelationType::BlockedBy: return item.links.blocked_by;
    }
    throw std::runtime_error("Unsupported relation type");
}

std::string relation_worklog_message(
    const std::string& operation,
    RelationType type,
    const RelationEndpoint& target,
    const std::optional<std::string>& idempotency_key
) {
    std::ostringstream out;
    out << "Relation " << operation << ": " << to_string(type) << " " << target.product << ":" << target.item_id;
    if (idempotency_key && !idempotency_key->empty()) {
        out << " [idempotency_key=" << *idempotency_key << "]";
    }
    return out.str();
}

void validate_mutation_request(const RelationMutationRequest& request) {
    require_safe_identifier(request.source_product, "source_product");
    require_safe_identifier(request.target_product, "target_product");
    require_safe_item_ref(request.source_item, "source_item");
    require_safe_item_ref(request.target_item, "target_item");
    if (request.agent.empty()) {
        throw std::runtime_error("agent is required");
    }
    if (request.max_products == 0 || request.max_items == 0) {
        throw std::runtime_error("relation scan limits must be greater than zero");
    }
    if (request.idempotency_key) {
        require_safe_identifier(*request.idempotency_key, "idempotency_key");
    }
}

RelationMutationResult remove_unresolved_target_relation(
    const RelationMutationRequest& request,
    const Catalog& catalog,
    const ProductRuntime& source_product,
    const ProductRuntime& target_product,
    const RelationEndpoint& source,
    const RelationEndpoint& target
) {
    RelationMutationResult result;
    result.operation = "remove";
    result.relation = requested_entry(source, target, request.relation_type);
    result.products_scanned = catalog.products.size();
    result.items_scanned = 1;
    result.unresolved_links = 1;

    CanonicalStore source_store(source_product.context.product_root);
    RefResolver source_resolver(source_store);
    auto source_item = source_resolver.resolve(source.item_id);
    const auto& current_values = relation_values(source_item, request.relation_type);
    const auto current_matches = std::count(current_values.begin(), current_values.end(), target.item_id);
    if (current_matches == 0) {
        result.status = "already_absent";
        result.already_in_desired_state = true;
        return result;
    }
    if (current_matches > 1) {
        throw std::runtime_error("Duplicate unresolved relation storage detected; refusing multi-value removal");
    }
    if (!request.apply) {
        result.status = "dry_run";
        result.changed = true;
        return result;
    }

    CanonicalStore target_store(target_product.context.product_root);
    if (!target_store.find_item_paths_by_id(target.item_id).empty()) {
        throw std::runtime_error("Missing relation target appeared before apply; retry through canonical removal: " + target.item_id);
    }

    source_item = source_resolver.resolve(source.item_id);
    auto& values = relation_values(source_item, request.relation_type);
    if (std::count(values.begin(), values.end(), target.item_id) > 1) {
        throw std::runtime_error("Duplicate unresolved relation storage detected before apply; refusing multi-value removal");
    }
    const auto before_size = values.size();
    values.erase(std::remove(values.begin(), values.end(), target.item_id), values.end());
    if (values.size() == before_size) {
        result.status = "already_absent";
        result.already_in_desired_state = true;
        return result;
    }
    StateMachine::record_worklog(
        source_item,
        request.agent,
        relation_worklog_message("removed unresolved target", request.relation_type, target, request.idempotency_key),
        request.model);
    source_store.write(source_item);
    result.applied = true;
    result.changed = true;
    result.worklog_appended = true;

    BacklogIndex index(source_product.context.backlog_root / ".cache" / "index" / "backlog.db");
    index.initialize();
    index.index_item(source_item);
    result.index_refreshed = true;

    auto readback = source_resolver.resolve(source.item_id);
    const auto& readback_values = relation_values(readback, request.relation_type);
    result.read_after_write =
        std::find(readback_values.begin(), readback_values.end(), target.item_id) == readback_values.end();
    result.status = result.read_after_write ? "removed" : "read_after_write_failed";
    result.safe_retry = result.read_after_write;
    return result;
}

RelationMutationResult mutate(const RelationMutationRequest& request, bool add) {
    validate_mutation_request(request);
    auto catalog = load_catalog(request.start_path, request.max_products);
    const auto& source_product = resolve_product(catalog, request.source_product, "source_product");
    const auto& target_product = resolve_product(catalog, request.target_product, "target_product");
    const auto source = resolve_endpoint(source_product, request.source_item, "source_item");
    const auto target_resolution = resolve_mutation_target(target_product, request.target_item, add);
    const auto target = target_resolution.endpoint;
    if (!add && !target_resolution.exists) {
        return remove_unresolved_target_relation(
            request, catalog, source_product, target_product, source, target);
    }
    if (same_endpoint(source, target)) {
        throw std::runtime_error("Self relations are not allowed");
    }

    const auto requested = requested_entry(source, target, request.relation_type);
    auto scan = scan_catalog(catalog, request.max_items);
    auto matches = matching_relations(scan, requested);

    RelationMutationResult result;
    result.operation = add ? "add" : "remove";
    result.relation = requested;
    result.products_scanned = catalog.products.size();
    result.items_scanned = scan.items_scanned;
    result.unresolved_links = scan.unresolved_links;

    if (add && !matches.empty()) {
        result.status = "already_present";
        result.already_in_desired_state = true;
        return result;
    }
    if (!add && matches.empty()) {
        result.status = "already_absent";
        result.already_in_desired_state = true;
        return result;
    }
    if (!add && matches.size() > 1) {
        throw std::runtime_error("Duplicate canonical relation storage detected; refusing non-atomic multi-owner removal");
    }
    if (add) {
        result.cycle_path = find_cycle_path(scan, requested);
        if (!result.cycle_path.empty()) {
            result.status = "cycle_rejected";
            return result;
        }
    }
    if (!request.apply) {
        result.status = "dry_run";
        result.changed = true;
        return result;
    }

    // Re-resolve endpoints and rescan immediately before the confirmed write.
    catalog = load_catalog(request.start_path, request.max_products);
    const auto& fresh_source_product = resolve_product(catalog, request.source_product, "source_product");
    const auto& fresh_target_product = resolve_product(catalog, request.target_product, "target_product");
    const auto fresh_source = resolve_endpoint(fresh_source_product, request.source_item, "source_item");
    const auto fresh_target = resolve_endpoint(fresh_target_product, request.target_item, "target_item");
    const auto fresh_requested = requested_entry(fresh_source, fresh_target, request.relation_type);
    scan = scan_catalog(catalog, request.max_items);
    matches = matching_relations(scan, fresh_requested);
    result.products_scanned = catalog.products.size();
    result.items_scanned = scan.items_scanned;
    result.unresolved_links = scan.unresolved_links;
    result.relation = fresh_requested;

    if (add && !matches.empty()) {
        result.status = "already_present";
        result.already_in_desired_state = true;
        return result;
    }
    if (!add && matches.empty()) {
        result.status = "already_absent";
        result.already_in_desired_state = true;
        return result;
    }
    if (!add && matches.size() > 1) {
        throw std::runtime_error("Duplicate canonical relation storage detected; refusing non-atomic multi-owner removal");
    }
    if (add) {
        result.cycle_path = find_cycle_path(scan, fresh_requested);
        if (!result.cycle_path.empty()) {
            result.status = "cycle_rejected";
            return result;
        }
    }

    RelationEndpoint owner_endpoint = fresh_source;
    RelationEndpoint stored_target = fresh_target;
    RelationType stored_type = request.relation_type;
    if (!add) {
        owner_endpoint = matches.front().stored_source;
        stored_target = matches.front().stored_target;
        stored_type = matches.front().stored_relation_type;
    }
    const auto& owner_product = product_by_name(catalog, owner_endpoint.product);
    CanonicalStore owner_store(owner_product.context.product_root);
    RefResolver owner_resolver(owner_store);
    auto owner_item = owner_resolver.resolve(owner_endpoint.item_id);
    auto& values = relation_values(owner_item, stored_type);
    if (add) {
        values.push_back(stored_target.item_id);
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    } else {
        values.erase(std::remove(values.begin(), values.end(), stored_target.item_id), values.end());
    }
    StateMachine::record_worklog(
        owner_item,
        request.agent,
        relation_worklog_message(add ? "added" : "removed", stored_type, stored_target, request.idempotency_key),
        request.model);
    owner_store.write(owner_item);
    result.applied = true;
    result.changed = true;
    result.worklog_appended = true;

    BacklogIndex index(owner_product.context.backlog_root / ".cache" / "index" / "backlog.db");
    index.initialize();
    index.index_item(owner_item);
    result.index_refreshed = true;

    auto readback = owner_resolver.resolve(owner_endpoint.item_id);
    const auto& readback_values = relation_values(readback, stored_type);
    const bool present = std::find(readback_values.begin(), readback_values.end(), stored_target.item_id) != readback_values.end();
    result.read_after_write = add ? present : !present;
    if (!result.read_after_write) {
        result.status = "read_after_write_failed";
        result.safe_retry = false;
        return result;
    }
    result.status = add ? "added" : "removed";
    return result;
}

std::size_t parse_cursor(const std::optional<std::string>& cursor) {
    if (!cursor || cursor->empty()) {
        return 0;
    }
    constexpr const char* prefix = "relation-v1:";
    if (cursor->rfind(prefix, 0) != 0) {
        throw std::runtime_error("Invalid relation cursor");
    }
    const std::string offset = cursor->substr(std::char_traits<char>::length(prefix));
    if (offset.empty() || !std::all_of(offset.begin(), offset.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        throw std::runtime_error("Invalid relation cursor");
    }
    return static_cast<std::size_t>(std::stoull(offset));
}

RelationEntry orient_for_item(const RelationEntry& entry, const RelationEndpoint& item) {
    RelationEntry oriented = entry;
    if (entry.relation_type == RelationType::Blocks && same_endpoint(entry.target, item)) {
        oriented.relation_type = RelationType::BlockedBy;
    }
    return oriented;
}

} // namespace

std::string to_string(RelationType type) {
    switch (type) {
        case RelationType::Relates: return "relates";
        case RelationType::Blocks: return "blocks";
        case RelationType::BlockedBy: return "blocked_by";
    }
    return "unknown";
}

std::optional<RelationType> parse_relation_type(const std::string& value) {
    const std::string normalized = lower_copy(trim_copy(value));
    if (normalized == "relates") return RelationType::Relates;
    if (normalized == "blocks") return RelationType::Blocks;
    if (normalized == "blocked_by" || normalized == "blocked-by") return RelationType::BlockedBy;
    return std::nullopt;
}

std::string to_string(RelationDirection direction) {
    switch (direction) {
        case RelationDirection::Outgoing: return "outgoing";
        case RelationDirection::Incoming: return "incoming";
        case RelationDirection::Both: return "both";
    }
    return "unknown";
}

std::optional<RelationDirection> parse_relation_direction(const std::string& value) {
    const std::string normalized = lower_copy(trim_copy(value));
    if (normalized == "outgoing") return RelationDirection::Outgoing;
    if (normalized == "incoming") return RelationDirection::Incoming;
    if (normalized == "both") return RelationDirection::Both;
    return std::nullopt;
}

RelationMutationResult RelationOps::add(const RelationMutationRequest& request) {
    return mutate(request, true);
}

RelationMutationResult RelationOps::remove(const RelationMutationRequest& request) {
    return mutate(request, false);
}

RelationListResult RelationOps::list(const RelationListRequest& request) {
    if (request.limit == 0 || request.limit > 500) {
        throw std::runtime_error("relation list limit must be between 1 and 500");
    }
    require_safe_identifier(request.product, "product");
    require_safe_item_ref(request.item, "item");
    auto catalog = load_catalog(request.start_path, request.max_products);
    const auto& product = resolve_product(catalog, request.product, "product");
    const auto selected = resolve_endpoint(product, request.item, "item");
    const auto scan = scan_catalog(catalog, request.max_items);

    std::vector<RelationEntry> matches;
    for (const auto& relation : scan.relations) {
        const bool outgoing = same_endpoint(relation.source, selected);
        const bool incoming = same_endpoint(relation.target, selected);
        const bool direction_match =
            request.direction == RelationDirection::Both ? (outgoing || incoming) :
            request.direction == RelationDirection::Outgoing ? outgoing : incoming;
        if (!direction_match) {
            continue;
        }
        auto oriented = orient_for_item(relation, selected);
        if (request.relation_type && oriented.relation_type != *request.relation_type) {
            continue;
        }
        matches.push_back(std::move(oriented));
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return std::tie(left.source.product, left.source.item_id, left.target.product, left.target.item_id,
                   left.relation_type, left.stored_source.product, left.stored_source.item_id) <
            std::tie(right.source.product, right.source.item_id, right.target.product, right.target.item_id,
                   right.relation_type, right.stored_source.product, right.stored_source.item_id);
    });

    RelationListResult result;
    result.item = selected;
    result.products_scanned = catalog.products.size();
    result.items_scanned = scan.items_scanned;
    result.unresolved_links = scan.unresolved_links;
    result.total_matches = matches.size();
    const std::size_t offset = parse_cursor(request.cursor);
    if (offset > matches.size()) {
        throw std::runtime_error("Relation cursor is beyond the result set");
    }
    const std::size_t end = std::min(matches.size(), offset + request.limit);
    result.relations.insert(result.relations.end(), matches.begin() + static_cast<std::ptrdiff_t>(offset),
        matches.begin() + static_cast<std::ptrdiff_t>(end));
    if (end < matches.size()) {
        result.next_cursor = "relation-v1:" + std::to_string(end);
    }
    return result;
}

} // namespace kano::backlog_ops
