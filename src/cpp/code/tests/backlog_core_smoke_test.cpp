#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_core/refs/ref_parser.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/validation/validator.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << text;
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    using kano::backlog_core::BacklogItem;
    using kano::backlog_core::BacklogContext;
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::Frontmatter;
    using kano::backlog_core::FrontmatterContext;
    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_core::ProjectConfig;
    using kano::backlog_core::RefParser;
    using kano::backlog_core::RefResolver;
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

        const auto alias_sections = Frontmatter::parse_body_sections(
            "# Context\n\nLegacy item context.\n\n"
            "# Non-Goals / Do Not\n\nDo not change release gates.\n\n"
            "# Intent Amendments\n\n2026-06-20: Human clarified scope.\n\n"
            "# Worklog\n\n2026-06-20 00:00 [agent=opencode] Created\n"
        );
        expect(alias_sections.at("context") == "Legacy item context.", "context section should parse");
        expect(alias_sections.at("non_goals") == "Do not change release gates.", "Non-Goals / Do Not alias should parse as non_goals");
        expect(alias_sections.at("intent_amendments") == "2026-06-20: Human clarified scope.", "intent amendments should parse");

        const auto legacy_sections = Frontmatter::parse_body_sections(
            "# Non-Goals\n\nKeep old headings readable.\n"
        );
        expect(legacy_sections.at("non_goals") == "Keep old headings readable.", "legacy Non-Goals heading should still parse");

        const std::string rendered_sections = Frontmatter::serialize_body_sections({
            {"context", "Canonical context."},
            {"non_goals", "Do not add required fields."},
            {"intent_amendments", "2026-06-20: Preserve chronology."},
            {"worklog", "2026-06-20 00:00 [agent=opencode] Rendered"}
        });
        expect(rendered_sections.find("# Non-Goals / Do Not") != std::string::npos, "canonical render should use Non-Goals / Do Not heading");
        expect(rendered_sections.find("# Intent Amendments") != std::string::npos, "canonical render should include Intent Amendments heading");
        expect(rendered_sections.find("# Context") < rendered_sections.find("# Non-Goals / Do Not"), "context should render before non-goals");
        expect(rendered_sections.find("# Non-Goals / Do Not") < rendered_sections.find("# Intent Amendments"), "non-goals should render before intent amendments");
        expect(rendered_sections.find("# Intent Amendments") < rendered_sections.find("# Worklog"), "intent amendments should render before worklog");

        const auto temp_root = std::filesystem::temp_directory_path() / "kano-backlog-core-intent-sections-smoke";
        std::filesystem::remove_all(temp_root);
        std::filesystem::create_directories(temp_root / "items" / "task" / "0000");
        CanonicalStore store(temp_root);
        BacklogItem persisted = item;
        persisted.file_path = temp_root / "items" / "task" / "0000" / "GT-TSK-0001_native-core-smoke.md";
        persisted.non_goals = "Do not mutate unrelated dirty files.";
        persisted.intent_amendments = "2026-06-20: Add optional sections only.";
        store.write(persisted);
        const auto reloaded = store.read(*persisted.file_path);
        expect(reloaded.non_goals == persisted.non_goals, "canonical store should round-trip non_goals");
        expect(reloaded.intent_amendments == persisted.intent_amendments, "canonical store should round-trip intent_amendments");
        const auto adjacent_index = persisted.file_path->parent_path() /
            (persisted.file_path->stem().string() + ".index.md");
        write_text(adjacent_index, "# Derived Epic index\n");
        const auto listed_items = store.list_items();
        expect(listed_items.size() == 1 && listed_items.front() == *persisted.file_path,
            "canonical item enumeration should exclude adjacent .index.md navigation artifacts");
        std::filesystem::remove_all(temp_root);

        expect(StateMachine::can_transition(ItemState::Ready, StateAction::Start),
            "ready should transition via Start");
        expect(StateMachine::can_transition(ItemState::Planned, StateAction::Start),
            "legacy planned should transition via Start");
        expect(!StateMachine::can_transition(ItemState::Done, StateAction::Start),
            "done should not transition via Start");
        expect(StateMachine::can_transition(ItemState::Review, StateAction::Reopen),
            "review should transition via explicit Reopen");
        expect(!StateMachine::can_transition(ItemState::Review, StateAction::Start),
            "review should not transition via Start");
        expect(!StateMachine::can_transition(ItemState::Done, StateAction::Reopen),
            "done should not transition via Reopen");

        BacklogItem transitioned = item;
        StateMachine::transition(transitioned, StateAction::Start, std::string("opencode"), std::string("Start work"));
        expect(transitioned.state == ItemState::InProgress, "transition should set InProgress");
        expect(transitioned.worklog.back().find("[agent=opencode]") != std::string::npos, "transition should include agent marker");
        expect(transitioned.worklog.back().find("[model=unknown]") == std::string::npos, "transition should omit unknown model marker");

        BacklogItem planned = item;
        planned.state = ItemState::Planned;
        StateMachine::transition(planned, StateAction::Start, std::string("opencode"), std::string("Start legacy planned work"));
        expect(planned.state == ItemState::InProgress, "legacy planned should transition to InProgress");

        BacklogItem reopened = item;
        reopened.state = ItemState::Review;
        bool reopen_without_rationale_rejected = false;
        try {
            StateMachine::transition(reopened, StateAction::Reopen, std::string("opencode"));
        } catch (const std::exception&) {
            reopen_without_rationale_rejected = true;
        }
        expect(reopen_without_rationale_rejected, "reopen should require rationale");
        StateMachine::transition(reopened, StateAction::Reopen, std::string("opencode"), std::string("Acceptance criteria remain unmet."));
        expect(reopened.state == ItemState::InProgress, "reopen should restore InProgress");
        expect(reopened.worklog.back().find("State: Review -> InProgress") != std::string::npos,
            "reopen should preserve source and target states in worklog");

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

        BacklogItem subtask = item;
        subtask.id = "GT-SUBTSK-0001";
        subtask.uid = "019cdf6a-0000-7000-8000-000000000004";
        subtask.type = ItemType::SubTask;
        subtask.title = "Native SubTask smoke";
        subtask.context = "Need first-class SubTask coverage.";
        subtask.goal = "Validate SubTask parsing, schema, refs, and Ready gate behavior.";
        subtask.approach = "Exercise the native C++ model and validator paths.";
        subtask.acceptance_criteria = "SubTask uses SUBTSK and task-style Ready fields.";
        subtask.risks = "Low.";
        auto subtask_schema_errors = Validator::validate_schema(subtask);
        expect(subtask_schema_errors.empty(), "subtask item should satisfy schema validation");
        auto [subtask_ready_ok, subtask_ready_gaps] = Validator::is_ready(subtask);
        expect(subtask_ready_ok, "subtask item should satisfy task-style ready gate");
        expect(subtask_ready_gaps.empty(), "subtask item should not have ready gate gaps");
        expect(to_string(ItemType::SubTask) == "SubTask", "subtask type should stringify");
        expect(parse_item_type("subtask").value_or(ItemType::Task) == ItemType::SubTask, "subtask type should parse");
        expect(parse_item_type("SubTask").value_or(ItemType::Task) == ItemType::SubTask, "SubTask type should parse case-insensitively");
        expect(parse_item_type("sub-task").value_or(ItemType::Task) == ItemType::SubTask, "sub-task alias should parse");
        expect(parse_item_type("sub_task").value_or(ItemType::Task) == ItemType::SubTask, "sub_task alias should parse");
        auto subtask_ref = RefParser::parse_display_id("GT-SUBTSK-0001");
        expect(subtask_ref.has_value(), "SUBTSK display id should parse");
        expect(subtask_ref->type_abbrev == "SUBTSK", "SUBTSK display id should preserve type abbreviation");

        BacklogItem reference_source = item;
        reference_source.links.relates = {"GT-TSK-0002"};
        reference_source.decisions = {
            "Evidence sentence with a source/path marker (source: implementation/preflight).",
            "Canonical dependency GT-BUG-0003 remains reviewable in prose."
        };
        reference_source.links.blocks = {"019cdf6a-0000-7000-8000-000000000099"};
        reference_source.context =
            "Context also names ADR-0013 for explicit validation. External thread "
            "019cdf6a-0000-7000-8000-000000000098 is provenance, not a backlog link. "
            "Canonical remap: GT-TSK-9998 from isolated GT-FTR-9998 is represented by "
            "GT-TSK-0002 under GT-FTR-0002. Follow-up GT-BUG-0004 remains active. "
            "The legacy GT-BUG-9998 label and mislabeled "
            "GT-BUG-9997 commit remain historical evidence.";
        const auto extracted_refs = RefResolver::get_references(reference_source);
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-TSK-0002") != extracted_refs.end(),
            "structured item links should remain references");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-0003") != extracted_refs.end(),
            "canonical tokens embedded in decision prose should remain references");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "ADR-0013") != extracted_refs.end(),
            "canonical tokens embedded in body prose should remain references");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "019cdf6a-0000-7000-8000-000000000099") != extracted_refs.end(),
            "structured UUID links should remain references");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "019cdf6a-0000-7000-8000-000000000098") == extracted_refs.end(),
            "free-form UUIDv7 provenance should not become a backlog reference");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-TSK-9998") == extracted_refs.end() &&
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-FTR-9998") == extracted_refs.end(),
            "explicit canonical-remap source IDs should remain historical prose");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-9998") == extracted_refs.end() &&
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-9997") == extracted_refs.end(),
            "legacy and mislabeled IDs should remain historical prose");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-TSK-0002") != extracted_refs.end() &&
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-FTR-0002") != extracted_refs.end(),
            "canonical-remap target IDs should remain active references");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-0004") != extracted_refs.end(),
            "canonical-remap suppression should not cross into the next sentence");
        expect(std::none_of(extracted_refs.begin(), extracted_refs.end(), [](const std::string& ref) {
            return ref.find("Evidence sentence") != std::string::npos;
        }), "decision prose should not become a whole path reference");

        BacklogItem incomplete_subtask = subtask;
        incomplete_subtask.approach.reset();
        auto [incomplete_subtask_ready, incomplete_subtask_gaps] = Validator::is_ready(incomplete_subtask);
        expect(!incomplete_subtask_ready, "subtask missing Approach should fail ready gate");
        expect(!incomplete_subtask_gaps.empty(), "subtask missing Approach should report a gap");

        BacklogItem initiative = item;
        initiative.id = "GT-INIT-0001";
        initiative.uid = "019cdf6a-0000-7000-8000-000000000003";
        initiative.type = ItemType::Initiative;
        initiative.title = "Native Initiative smoke";
        initiative.context = "Initiative captures an independently releasable component narrative.";
        initiative.goal = "Validate Initiative as a hard formal item type.";
        initiative.approach.reset();
        initiative.acceptance_criteria.reset();
        initiative.risks.reset();
        auto initiative_schema_errors = Validator::validate_schema(initiative);
        expect(initiative_schema_errors.empty(), "initiative item should satisfy schema validation");
        auto [initiative_ready_ok, initiative_ready_gaps] = Validator::is_ready(initiative);
        expect(initiative_ready_ok, "initiative item should satisfy the light ready gate");
        expect(initiative_ready_gaps.empty(), "initiative item should not need task ready fields");
        expect(to_string(ItemType::Initiative) == "Initiative", "initiative type should stringify");
        expect(parse_item_type("initiative").value_or(ItemType::Task) == ItemType::Initiative, "initiative type should parse");
        expect(parse_item_type("Initiative").value_or(ItemType::Task) == ItemType::Initiative, "Initiative type should parse case-insensitively");

        BacklogItem incomplete_initiative = initiative;
        incomplete_initiative.goal.reset();
        auto [incomplete_initiative_ready, incomplete_initiative_gaps] = Validator::is_ready(incomplete_initiative);
        expect(!incomplete_initiative_ready, "initiative missing Goal should fail ready gate");
        expect(!incomplete_initiative_gaps.empty(), "initiative missing Goal should report a gap");

        const auto config_root = std::filesystem::temp_directory_path() / "kano-backlog-core-config-defaults-smoke";
        std::filesystem::remove_all(config_root);
        write_text(
            config_root / ".kano" / "backlog_config.toml",
            "[products.demo]\n"
            "name = \"demo\"\n"
            "prefix = \"DEM\"\n"
            "backlog_root = \"_kano/backlog/products/demo\"\n"
            "default_assignee = \"agent-default\"\n"
            "default_bug_reviewer = \"review-default\"\n");
        write_text(
            config_root / "_kano" / "backlog" / "products" / "demo" / "_config" / "config.toml",
            "[product]\n"
            "name = \"demo\"\n"
            "prefix = \"DEM\"\n"
            "default_assignee = \"koa\"\n"
            "default_bug_reviewer = \"reviewer-koa\"\n");

        auto project_config = ProjectConfig::load_from_toml(config_root / ".kano" / "backlog_config.toml");
        expect(project_config.has_value(), "project config should parse");
        expect(project_config->products.at("demo").default_assignee.value_or("") == "agent-default",
               "project config should parse default_assignee");
        expect(project_config->products.at("demo").default_bug_reviewer.value_or("") == "review-default",
               "project config should parse default_bug_reviewer");
        expect(project_config->resolve_product_name("demo").value_or("") == "demo",
               "project config should resolve the canonical product name");
        expect(project_config->resolve_product_name(" dem ").value_or("") == "demo",
               "project config should resolve a normalized product prefix");

        auto resolved_context = BacklogContext::resolve(config_root, std::optional<std::string>("demo"), std::nullopt);
        expect(resolved_context.product_def.default_assignee.value_or("") == "koa",
               "product-local config should override default_assignee");
        expect(resolved_context.product_def.default_bug_reviewer.value_or("") == "reviewer-koa",
               "product-local config should override default_bug_reviewer");
        auto prefix_context = BacklogContext::resolve(config_root, std::optional<std::string>("dem"), std::nullopt);
        expect(prefix_context.product_name == "demo",
               "product prefix should resolve to the canonical product name");
        expect(prefix_context.product_root == resolved_context.product_root,
               "product prefix should resolve to the canonical product root");
        expect(prefix_context.product_def.prefix == "DEM",
               "product prefix context should load the canonical product definition");
        std::filesystem::remove_all(config_root);

        std::cout << "backlog_core_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "backlog_core_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
