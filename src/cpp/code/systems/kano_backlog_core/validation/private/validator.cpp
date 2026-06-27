#include "kano/backlog_core/validation/validator.hpp"
#include <algorithm>
#include <array>
#include <regex>

namespace kano::backlog_core {

namespace {

bool is_blank_text(const std::string& value) {
    return value.find_first_not_of(" \n\r\t") == std::string::npos;
}

bool is_allowed_work_intent(const std::string& value) {
    static constexpr std::array<const char*, 17> allowed = {
        "implementation",
        "investigation",
        "spike",
        "decision",
        "experiment",
        "validation",
        "audit",
        "migration",
        "policy_contract",
        "decomposition",
        "planning",
        "docs_only",
        "review_admission",
        "parent_reconciliation",
        "runbook",
        "incident",
        "deprecation"
    };
    return std::any_of(allowed.begin(), allowed.end(), [&](const char* allowed_value) {
        return value == allowed_value;
    });
}

} // namespace

std::pair<bool, std::vector<std::string>> Validator::is_ready(const BacklogItem& item) {
    std::vector<std::string> missing;

    auto check_field = [&](const std::optional<std::string>& field, const std::string& name) {
        if (!field || field->empty() || field->find_first_not_of(" \n\r\t") == std::string::npos) {
            missing.push_back(name);
        }
    };

    switch (item.type) {
        case ItemType::Initiative:
        case ItemType::Epic:
            check_field(item.context, "Context");
            check_field(item.goal, "Goal");
            break;
        case ItemType::Feature:
        case ItemType::UserStory:
            check_field(item.context, "Context");
            check_field(item.goal, "Goal");
            check_field(item.acceptance_criteria, "Acceptance Criteria");
            break;
        case ItemType::Task:
        case ItemType::Bug:
        case ItemType::Issue:
            check_field(item.context, "Context");
            check_field(item.goal, "Goal");
            check_field(item.approach, "Approach");
            check_field(item.acceptance_criteria, "Acceptance Criteria");
            check_field(item.risks, "Risks / Dependencies");
            break;
    }

    return {missing.empty(), missing};
}

std::vector<std::string> Validator::validate_schema(const BacklogItem& item) {
    std::vector<std::string> errors;

    // 1. Required fields
    if (item.id.empty()) errors.push_back("Missing required field: id");
    if (item.uid.empty()) errors.push_back("Missing required field: uid");
    if (item.title.empty()) errors.push_back("Missing required field: title");
    if (item.created.empty()) errors.push_back("Missing required field: created");
    if (item.updated.empty()) errors.push_back("Missing required field: updated");

    // 2. Validate ID format
    // Pattern: ^[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|BUG|ISS)-\d{4}$
    static const std::regex id_regex(R"(^[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|BUG|ISS)-\d{4}$)");
    if (!item.id.empty() && !std::regex_match(item.id, id_regex)) {
        errors.push_back("Invalid id format: " + item.id + " (expected <PREFIX>-(INIT|EPIC|FTR|USR|TSK|BUG|ISS)-<NNNN>)");
    }

    // 3. Validate UID format (strict UUIDv7)
    // Pattern: ^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$
    static const std::regex uuid_v7_regex(R"(^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)");
    if (!item.uid.empty() && !std::regex_match(item.uid, uuid_v7_regex)) {
        errors.push_back("Invalid uid format: " + item.uid + " (expected UUIDv7)");
    }

    // 4. Validate dates (ISO format YYYY-MM-DD)
    static const std::regex date_regex(R"(^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])$)");
    if (!item.created.empty() && !std::regex_match(item.created, date_regex)) {
        errors.push_back("Invalid created date format: " + item.created + " (expected YYYY-MM-DD)");
    }
    if (!item.updated.empty() && !std::regex_match(item.updated, date_regex)) {
        errors.push_back("Invalid updated date format: " + item.updated + " (expected YYYY-MM-DD)");
    }

    if (item.state == ItemState::Duplicate) {
        if (!item.duplicate_of || item.duplicate_of->find_first_not_of(" \n\r\t") == std::string::npos) {
            errors.push_back("Duplicate state requires duplicate_of canonical target");
        } else if (*item.duplicate_of == item.id || *item.duplicate_of == item.uid) {
            errors.push_back("Duplicate item cannot reference itself in duplicate_of");
        }
    }

    if (item.work_intent && !is_blank_text(*item.work_intent) && !is_allowed_work_intent(*item.work_intent)) {
        errors.push_back(
            "Invalid work_intent: " + *item.work_intent +
            " (expected one of: implementation, investigation, spike, decision, experiment, validation, audit, migration, policy_contract, decomposition, planning, docs_only, review_admission, parent_reconciliation, runbook, incident, deprecation)");
    }

    return errors;
}

} // namespace kano::backlog_core
