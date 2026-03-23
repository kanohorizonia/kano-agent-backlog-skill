#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kano::backlog::core {

enum class ItemType { Epic, Feature, UserStory, Task, Bug };
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

using ExternalFields = std::unordered_map<std::string, std::string>;
using LinkMap = std::unordered_map<std::string, std::vector<std::string>>;

struct BacklogItem {
    std::string id;
    std::string uid;
    ItemType type;
    std::string title;
    ItemState state;
    std::optional<std::string> priority;
    std::optional<std::string> parent;
    std::optional<std::string> owner;
    std::vector<std::string> tags;
    std::optional<std::string> area;
    std::optional<std::string> iteration;
    ExternalFields external;
    LinkMap links;
    std::vector<std::string> decisions;
    std::string created;
    std::string updated;

    std::optional<std::string> context;
    std::optional<std::string> goal;
    std::optional<std::string> non_goals;
    std::optional<std::string> approach;
    std::optional<std::string> alternatives;
    std::optional<std::string> acceptance_criteria;
    std::optional<std::string> risks;
    std::vector<std::string> worklog;
};

inline const char* to_string(ItemType type) {
    switch (type) {
    case ItemType::Epic:
        return "Epic";
    case ItemType::Feature:
        return "Feature";
    case ItemType::UserStory:
        return "UserStory";
    case ItemType::Task:
        return "Task";
    case ItemType::Bug:
        return "Bug";
    }
    return "Unknown";
}

inline const char* to_string(ItemState state) {
    switch (state) {
    case ItemState::New:
        return "New";
    case ItemState::Proposed:
        return "Proposed";
    case ItemState::Planned:
        return "Planned";
    case ItemState::Ready:
        return "Ready";
    case ItemState::InProgress:
        return "InProgress";
    case ItemState::Review:
        return "Review";
    case ItemState::Done:
        return "Done";
    case ItemState::Blocked:
        return "Blocked";
    case ItemState::Dropped:
        return "Dropped";
    }
    return "Unknown";
}

} // namespace kano::backlog::core
