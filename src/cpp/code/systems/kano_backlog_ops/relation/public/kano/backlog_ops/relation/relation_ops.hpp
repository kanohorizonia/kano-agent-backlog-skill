#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kano::backlog_ops {

enum class RelationType {
    Relates,
    Blocks,
    BlockedBy,
};

enum class RelationDirection {
    Outgoing,
    Incoming,
    Both,
};

std::string to_string(RelationType type);
std::optional<RelationType> parse_relation_type(const std::string& value);
std::string to_string(RelationDirection direction);
std::optional<RelationDirection> parse_relation_direction(const std::string& value);

struct RelationEndpoint {
    std::string product;
    std::string item_id;
    std::string uid;
};

struct RelationEntry {
    RelationEndpoint source;
    RelationEndpoint target;
    RelationType relation_type = RelationType::Relates;
    RelationType stored_relation_type = RelationType::Relates;
    RelationEndpoint stored_source;
    RelationEndpoint stored_target;
};

struct RelationMutationRequest {
    std::filesystem::path start_path;
    std::string source_product;
    std::string source_item;
    std::string target_product;
    std::string target_item;
    RelationType relation_type = RelationType::Relates;
    std::string agent;
    std::optional<std::string> model;
    std::optional<std::string> idempotency_key;
    bool apply = false;
    std::size_t max_products = 64;
    std::size_t max_items = 20000;
};

struct RelationMutationResult {
    std::string operation;
    std::string status;
    bool applied = false;
    bool changed = false;
    bool already_in_desired_state = false;
    bool worklog_appended = false;
    bool index_refreshed = false;
    bool read_after_write = false;
    bool safe_retry = true;
    RelationEntry relation;
    std::vector<RelationEndpoint> cycle_path;
    std::size_t products_scanned = 0;
    std::size_t items_scanned = 0;
    std::size_t unresolved_links = 0;
};

struct RelationListRequest {
    std::filesystem::path start_path;
    std::string product;
    std::string item;
    std::optional<RelationType> relation_type;
    RelationDirection direction = RelationDirection::Both;
    std::size_t limit = 100;
    std::optional<std::string> cursor;
    std::size_t max_products = 64;
    std::size_t max_items = 20000;
};

struct RelationListResult {
    RelationEndpoint item;
    std::vector<RelationEntry> relations;
    std::optional<std::string> next_cursor;
    std::size_t total_matches = 0;
    std::size_t products_scanned = 0;
    std::size_t items_scanned = 0;
    std::size_t unresolved_links = 0;
};

class RelationOps {
public:
    static RelationMutationResult add(const RelationMutationRequest& request);
    static RelationMutationResult remove(const RelationMutationRequest& request);
    static RelationListResult list(const RelationListRequest& request);
};

} // namespace kano::backlog_ops
