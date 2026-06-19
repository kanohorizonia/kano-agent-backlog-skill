#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <stdexcept>

namespace kano::backlog_core {

enum class ItemType {
    Epic,
    Feature,
    UserStory,
    Task,
    Bug,
    Issue
};

enum class ItemState {
    New,
    Proposed,
    Planned,
    Ready,
    InProgress,
    Review,
    Done,
    Blocked,
    Dropped
};

enum class StateAction {
    Propose,
    Ready,
    Start,
    Review,
    Done,
    Block,
    Drop
};

struct WorklogEntry {
    std::string timestamp; // YYYY-MM-DD HH:MM
    std::string agent;
    std::optional<std::string> model;
    std::string message;

    // Helper functions can be declared here and defined in .cpp
    static std::optional<WorklogEntry> parse(const std::string& line);
    std::string format() const;
};

struct ItemLinks {
    std::vector<std::string> relates;
    std::vector<std::string> blocks;
    std::vector<std::string> blocked_by;
};

struct BacklogItem {
    // Frontmatter fields
    std::string id;
    std::string uid;
    ItemType type;
    std::string title;
    ItemState state;
    
    std::optional<std::string> priority;
    std::optional<std::string> parent;
    std::optional<std::string> owner;
    std::vector<std::string> tags;
    std::string created;
    std::string updated;
    std::optional<std::string> area;
    std::optional<std::string> iteration;
    
    std::map<std::string, std::string> external; // Simplified from Dict[str, Any]
    ItemLinks links;
    std::vector<std::string> decisions;

    // Body sections
    std::optional<std::string> context;
    std::optional<std::string> goal;
    std::optional<std::string> non_goals;
    std::optional<std::string> approach;
    std::optional<std::string> alternatives;
    std::optional<std::string> acceptance_criteria;
    std::optional<std::string> risks;
    std::vector<std::string> worklog; // Raw lines or could be std::vector<WorklogEntry> based on needs

    // Metadata
    std::optional<std::filesystem::path> file_path;
};

struct CreateItemResult {
    std::string id;
    std::string uid;
    std::filesystem::path path;
    ItemType type;
};

struct UpdateStateResult {
    std::string id;
    ItemState old_state;
    ItemState new_state;
    bool worklog_appended;
    bool parent_synced = false;
    bool dashboards_refreshed = false;
};

struct TrashItemResult {
    std::string item_ref;
    std::filesystem::path source_path;
    std::filesystem::path trashed_path;
    std::string status;
    std::optional<std::string> reason;
};

struct DecisionWritebackResult {
    std::string item_id;
    std::filesystem::path path;
    bool added;
    bool updated;
};

struct RemapIdResult {
    std::string old_id;
    std::string new_id;
    std::filesystem::path old_path;
    std::filesystem::path new_path;
    int updated_files;
};

// Utility function declarations
std::string to_string(ItemType type);
std::optional<ItemType> parse_item_type(const std::string& str);

std::string to_string(ItemState state);
std::optional<ItemState> parse_item_state(const std::string& str);

std::string to_string(StateAction action);
std::optional<StateAction> parse_state_action(const std::string& str);

} // namespace kano::backlog_core
