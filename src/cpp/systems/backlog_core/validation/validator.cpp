#include "validator.hpp"

#include <algorithm>
#include <cctype>

namespace kano::backlog::core {

namespace {

bool is_blank(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return true;
    }

    return std::all_of(value->begin(), value->end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

} // namespace

void Validator::require_field(
    const std::optional<std::string>& value,
    const std::string& display_name,
    std::vector<std::string>& errors) {
    if (is_blank(value)) {
        errors.push_back(display_name);
    }
}

ValidationResult Validator::validate_ready_gate(const BacklogItem& item) {
    std::vector<std::string> errors;

    switch (item.type) {
    case ItemType::Epic:
        require_field(item.context, "Context", errors);
        require_field(item.goal, "Goal", errors);
        break;
    case ItemType::Feature:
    case ItemType::UserStory:
        require_field(item.context, "Context", errors);
        require_field(item.goal, "Goal", errors);
        require_field(item.acceptance_criteria, "Acceptance Criteria", errors);
        break;
    case ItemType::Task:
    case ItemType::Bug:
        require_field(item.context, "Context", errors);
        require_field(item.goal, "Goal", errors);
        require_field(item.approach, "Approach", errors);
        require_field(item.acceptance_criteria, "Acceptance Criteria", errors);
        require_field(item.risks, "Risks / Dependencies", errors);
        break;
    }

    return ValidationResult{errors.empty(), std::move(errors)};
}

ValidationResult Validator::validate_item(const BacklogItem& item) {
    std::vector<std::string> errors;

    if (item.id.empty()) {
        errors.push_back("ID is required");
    }
    if (item.uid.empty()) {
        errors.push_back("UID is required");
    }
    if (item.title.empty()) {
        errors.push_back("Title is required");
    }

    ValidationResult ready_gate = validate_ready_gate(item);
    if (item.state == ItemState::Ready || item.state == ItemState::InProgress ||
        item.state == ItemState::Review || item.state == ItemState::Done) {
        errors.insert(errors.end(), ready_gate.errors.begin(), ready_gate.errors.end());
    }

    return ValidationResult{errors.empty(), std::move(errors)};
}

} // namespace kano::backlog::core
