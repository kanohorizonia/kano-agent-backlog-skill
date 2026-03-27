#include <iostream>
#include <stdexcept>

#include "backlog_core/frontmatter/parser.hpp"
#include "backlog_core/state/state_machine.hpp"
#include "backlog_core/validation/validator.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    using kano::backlog::core::BacklogItem;
    using kano::backlog::core::FrontmatterParser;
    using kano::backlog::core::ItemState;
    using kano::backlog::core::ItemType;
    using kano::backlog::core::StateMachine;
    using kano::backlog::core::Validator;

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

        auto validation = Validator::validate_item(item);
        expect(validation.valid, "ready item should validate");

        const std::string serialized = FrontmatterParser::serialize(item);
        const BacklogItem round_trip = FrontmatterParser::parse(serialized);

        expect(round_trip.id == item.id, "round-trip id mismatch");
        expect(round_trip.title == item.title, "round-trip title mismatch");
        expect(round_trip.context == item.context, "round-trip context mismatch");
        expect(round_trip.acceptance_criteria == item.acceptance_criteria,
            "round-trip acceptance criteria mismatch");
        expect(round_trip.worklog.size() == 1, "round-trip worklog size mismatch");

        expect(StateMachine::can_transition(ItemState::Ready, ItemState::InProgress),
            "ready should transition to in progress");
        expect(!StateMachine::can_transition(ItemState::Done, ItemState::InProgress),
            "done should not transition to in progress");
        expect(FrontmatterParser::parse_state("InProgress") == ItemState::InProgress,
            "parse_state should parse InProgress");

        BacklogItem invalid = item;
        invalid.approach.reset();
        auto invalid_validation = Validator::validate_item(invalid);
        expect(!invalid_validation.valid, "missing ready-gate field should fail validation");

        std::cout << "backlog_core_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "backlog_core_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
