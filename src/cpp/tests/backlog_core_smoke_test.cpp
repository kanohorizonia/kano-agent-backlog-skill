// Smoke test for backlog_core (new layer: code/systems/kano_backlog_core)
// Updated 2026-03-24 to match new API

#include <iostream>
#include <stdexcept>

#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/validation/validator.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    using kano::backlog_core::BacklogItem;
    using kano::backlog_core::Frontmatter;
    using kano::backlog_core::FrontmatterContext;
    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_core::StateMachine;
    using kano::backlog_core::StateAction;
    using kano::backlog_core::Validator;

    try {
        // Create a sample backlog item
        BacklogItem item;
        item.id = "GT-TSK-0001";
        item.uid = "019cdf6a-0000-7000-8000-000000000001";
        item.type = ItemType::Task;
        item.title = "Native core smoke";
        item.state = ItemState::Ready;
        item.created = "2026-03-12";
        item.updated = "2026-03-12";
        item.area = "general";
        item.iteration = "backlog";
        item.priority = "P2";
        item.external = {{"azure_id", "null"}, {"jira_key", "null"}};
        item.links.relates = {};
        item.links.blocks = {};
        item.links.blocked_by = {};
        item.context = "Need native smoke coverage.";
        item.goal = "Verify parser, validator, and state transitions.";
        item.approach = "Use deterministic C++ smoke tests.";
        item.acceptance_criteria = "CTest runs and passes.";
        item.risks = "Low.";
        item.worklog = {"2026-03-12 00:00 [agent=opencode] Created smoke item"};

        // Test Validator::is_ready
        auto [is_ready, missing] = Validator::is_ready(item);
        expect(is_ready, "ready item should pass ready gate");
        expect(missing.empty(), "ready item should have no missing fields");

        // Test Frontmatter round-trip
        // First, we need to manually construct a YAML frontmatter + body string
        // since Frontmatter::parse/serialize work on raw strings
        std::string sample_content = R"(---
id: GT-TSK-0001
uid: 019cdf6a-0000-7000-8000-000000000001
type: Task
title: Native core smoke
state: Ready
created: "2026-03-12"
updated: "2026-03-12"
area: general
iteration: backlog
priority: P2
---

# Context
Need native smoke coverage.

# Goal
Verify parser, validator, and state transitions.
)";

        FrontmatterContext ctx = Frontmatter::parse(sample_content);
        expect(!ctx.metadata.IsNull(), "parsed frontmatter should have metadata");
        expect(!ctx.body.empty(), "parsed frontmatter should have body");

        // Test StateMachine::can_transition (uses StateAction enum)
        expect(StateMachine::can_transition(ItemState::Ready, StateAction::Start),
            "ready should transition to in progress via Start action");
        expect(!StateMachine::can_transition(ItemState::Done, StateAction::Start),
            "done should not transition via Start action");

        // Test validation with missing field
        BacklogItem invalid = item;
        invalid.approach.reset();
        auto [invalid_ready, invalid_missing] = Validator::is_ready(invalid);
        expect(!invalid_ready, "missing approach should fail ready gate");
        expect(!invalid_missing.empty(), "missing fields should be reported");

        // Test schema validation
        auto schema_errors = Validator::validate_schema(item);
        expect(schema_errors.empty(), "valid item should have no schema errors");

        std::cout << "backlog_core_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "backlog_core_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
