#include <iostream>
#include <stdexcept>

#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
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
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    using kano::backlog_core::BacklogItem;
    using kano::backlog_core::Frontmatter;
    using kano::backlog_core::FrontmatterContext;
    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_core::StateAction;
    using kano::backlog_core::StateMachine;
    using kano::backlog_core::Validator;
    using kano::backlog_core::parse_item_type;

    try {
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
        item.links = {{"relates", {}}, {"blocks", {}}, {"blocked_by", {}}};
        item.context = "Need native smoke coverage.";
        item.goal = "Verify parser, validator, and state transitions.";
        item.approach = "Use deterministic C++ smoke tests.";
        item.acceptance_criteria = "CTest runs and passes.";
        item.risks = "Low.";
        item.worklog = {"2026-03-12 00:00 [agent=opencode] Created smoke item"};

        auto schema_errors = Validator::validate_schema(item);
        expect(schema_errors.empty(), "ready item should satisfy schema validation");
        auto [ready_ok, ready_gaps] = Validator::is_ready(item);
        expect(ready_ok, "ready item should satisfy ready gate");
        expect(ready_gaps.empty(), "ready item should satisfy ready gate");

        FrontmatterContext ctx;
        ctx.metadata["id"] = item.id;
        ctx.metadata["uid"] = item.uid;
        ctx.metadata["type"] = "task";
        ctx.metadata["title"] = item.title;
        ctx.metadata["state"] = "Ready";
        ctx.body = "# Context\n" + *item.context + "\n\n# Goal\n" + *item.goal + "\n";

        const std::string serialized = Frontmatter::serialize(ctx);
        const FrontmatterContext round_trip = Frontmatter::parse(serialized);

        expect(round_trip.metadata["id"].as<std::string>() == item.id, "round-trip id mismatch");
        expect(round_trip.metadata["title"].as<std::string>() == item.title, "round-trip title mismatch");
        expect(round_trip.body.find(*item.context) != std::string::npos, "round-trip context mismatch");

        expect(StateMachine::can_transition(ItemState::Ready, StateAction::Start),
            "ready should transition via Start");
        expect(!StateMachine::can_transition(ItemState::Done, StateAction::Start),
            "done should not transition via Start");

        BacklogItem transitioned = item;
        StateMachine::transition(transitioned, StateAction::Start, std::string("opencode"), std::string("Start work"));
        expect(transitioned.state == ItemState::InProgress, "transition should set InProgress");
        expect(transitioned.worklog.back().find("[agent=opencode]") != std::string::npos, "transition should include agent marker");
        expect(transitioned.worklog.back().find("[model=unknown]") == std::string::npos, "transition should omit unknown model marker");

        BacklogItem modeled = item;
        StateMachine::record_worklog(modeled, "opencode", "Record modeled work", std::string("gpt-test"));
        expect(modeled.worklog.back().find("[model=gpt-test]") != std::string::npos, "explicit model marker should be preserved");

        BacklogItem unmodeled = item;
        StateMachine::record_worklog(unmodeled, "opencode", "Record unmodeled work");
        expect(unmodeled.worklog.back().find("[agent=opencode]") != std::string::npos, "record_worklog should include agent marker");
        expect(unmodeled.worklog.back().find("[model=unknown]") == std::string::npos, "record_worklog should omit unknown model marker");

        BacklogItem invalid = item;
        invalid.approach.reset();
        auto [invalid_ready, invalid_ready_gaps] = Validator::is_ready(invalid);
        expect(!invalid_ready, "missing ready-gate field should fail validation");
        expect(!invalid_ready_gaps.empty(), "missing ready-gate field should fail validation");

        BacklogItem issue = item;
        issue.id = "GT-ISS-0001";
        issue.uid = "019cdf6a-0000-7000-8000-000000000002";
        issue.type = ItemType::Issue;
        issue.title = "Pre-triage runtime gap";
        issue.context = "A runtime gap is reported before the exact fix path is known.";
        issue.goal = "Capture the unclear problem, risk, and blocker evidence without forcing a Task or Bug classification.";
        issue.approach = "Triage the evidence, then split follow-up Tasks or Bugs once the remediation path is clear.";
        issue.acceptance_criteria = "Issue validates with ISS prefix and full Ready gate fields.";
        issue.risks = "Issue items must not imply new Research, Decision, or Spike item types.";
        auto issue_schema_errors = Validator::validate_schema(issue);
        expect(issue_schema_errors.empty(), "issue item should satisfy schema validation");
        auto [issue_ready_ok, issue_ready_gaps] = Validator::is_ready(issue);
        expect(issue_ready_ok, "issue item should satisfy ready gate");
        expect(issue_ready_gaps.empty(), "issue item should not have ready gate gaps");
        expect(to_string(ItemType::Issue) == "Issue", "issue type should stringify");
        expect(parse_item_type("issue").value_or(ItemType::Task) == ItemType::Issue, "issue type should parse");
        expect(parse_item_type("Issue").value_or(ItemType::Task) == ItemType::Issue, "Issue type should parse case-insensitively");

        std::cout << "backlog_core_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "backlog_core_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
