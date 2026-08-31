#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/models/errors.hpp"
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

class DisposableDirectory {
public:
    explicit DisposableDirectory(const std::filesystem::path& path) : path_(path) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error) {
            throw std::runtime_error("failed to clean " + path_.string());
        }
        std::filesystem::create_directories(path_, error);
        if (error) {
            throw std::runtime_error("failed to create " + path_.string());
        }
    }

    ~DisposableDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void expect_product_resolution(
    const kano::backlog_core::ProjectConfig& config,
    const std::string& selector,
    const std::string& expected_canonical_slug,
    kano::backlog_core::ProductResolutionKind expected_resolution_kind,
    const std::string& expected_matched_selector
) {
    const auto resolution = config.resolve_product(selector);
    expect(resolution.has_value(), "product selector should resolve: " + selector);
    expect(resolution->canonical_slug == expected_canonical_slug,
        "product selector resolved the wrong canonical product: " + selector);
    expect(resolution->resolution_kind == expected_resolution_kind,
        "product selector resolved with the wrong precedence kind: " + selector);
    expect(resolution->requested_selector == selector,
        "product resolution should preserve the requested selector: " + selector);
    expect(resolution->normalized_selector ==
            kano::backlog_core::ProjectConfig::normalize_product_selector(selector),
        "product resolution should preserve the normalized selector: " + selector);
    expect(resolution->matched_selector == expected_matched_selector,
        "product resolution should preserve the matched configured selector: " + selector);
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
    using kano::backlog_core::ProductResolutionKind;
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

        {
            const DisposableDirectory bounded_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-bounded-metadata-smoke");
            const auto item_directory = bounded_fixtures.path() / "items" / "task" / "0000";
            CanonicalStore bounded_store(bounded_fixtures.path());

            const std::string valid_frontmatter =
                "---\r\n"
                "id: GT-TSK-0002\r\n"
                "uid: 019cdf6a-0000-7000-8000-000000000002\r\n"
                "type: task\r\n"
                "title: Bounded metadata smoke\r\n"
                "state: Ready\r\n"
                "created: \"2026-08-29\"\r\n"
                "updated: \"2026-08-29\"\r\n"
                "---\r\n";
            std::string large_item = valid_frontmatter;
            constexpr std::size_t kLargeBodyBytes = 2 * 1024 * 1024;
            large_item.append(kLargeBodyBytes, 'x');
            const auto large_item_path = item_directory / "GT-TSK-0002_bounded-metadata-smoke.md";
            write_text(large_item_path, large_item);

            constexpr std::size_t kMaximumFrontmatterBytes = 8 * 1024;
            std::size_t valid_bytes_read = 0;
            const auto bounded_item = bounded_store.read_metadata_bounded(
                large_item_path,
                kMaximumFrontmatterBytes,
                &valid_bytes_read);
            expect(bounded_item.id == "GT-TSK-0002", "bounded metadata read should map the canonical item");
            expect(!bounded_item.context.has_value(), "bounded metadata read should skip body sections");
            expect(valid_bytes_read >= valid_frontmatter.size(),
                "bounded metadata read should include the closing delimiter");
            expect(valid_bytes_read <= valid_frontmatter.size() + 4096,
                "bounded metadata read should stop within one fixed chunk of frontmatter");
            expect(valid_bytes_read < large_item.size(),
                "bounded metadata read should not consume the multi-megabyte body");

            constexpr std::size_t kMalformedFrontmatterCap = 128;
            const auto malformed_item_path = item_directory / "GT-TSK-0003_unclosed-frontmatter.md";
            write_text(
                malformed_item_path,
                "---\n"
                "id: GT-TSK-0003\n" + std::string(2 * 1024, 'y'));

            std::size_t malformed_bytes_read = 0;
            bool byte_limit_thrown = false;
            try {
                static_cast<void>(bounded_store.read_metadata_bounded(
                    malformed_item_path,
                    kMalformedFrontmatterCap,
                    &malformed_bytes_read));
            } catch (const kano::backlog_core::ParseError& error) {
                byte_limit_thrown = true;
                expect(error.details == "frontmatter_byte_limit_exceeded",
                    "unclosed bounded frontmatter should report the exact byte-limit reason");
            }
            expect(byte_limit_thrown, "unclosed bounded frontmatter should throw ParseError");
            expect(malformed_bytes_read <= kMalformedFrontmatterCap,
                "unclosed bounded frontmatter should never report bytes beyond the cap");
        }

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

        BacklogItem review_started = item;
        review_started.state = ItemState::Review;
        std::string review_start_diagnostic;
        try {
            StateMachine::transition(
                review_started,
                StateAction::Start,
                std::string("opencode"),
                std::string("Resume review work"));
        } catch (const std::exception& ex) {
            review_start_diagnostic = ex.what();
        }
        expect(review_start_diagnostic.find("reopen") != std::string::npos,
            "invalid Review plus Start should recommend audited reopen");
        expect(review_start_diagnostic.find("workitem update-state") != std::string::npos,
            "invalid Review plus Start should recommend the generic target-state command");
        expect(review_started.state == ItemState::Review,
            "invalid Review plus Start should not mutate state");

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
            "Canonical dependency GT-BUG-0003 remains reviewable in prose.",
            "Historical identities GT-TSK-9998 and GT-TSK-9999 belong to this item."
        };
        reference_source.links.blocks = {"019cdf6a-0000-7000-8000-000000000099"};
        reference_source.context =
            "Context also names ADR-0013 for explicit validation. External thread "
            "019cdf6a-0000-7000-8000-000000000098 is provenance, not a backlog link. "
            "Current identity GT-TSK-0001 remains active. Canonical remap: isolated "
            "GT-FTR-9998 is represented by GT-FTR-0002. Follow-up GT-BUG-0004 remains active. "
            "The legacy GT-BUG-9998 label and mislabeled "
            "GT-BUG-9997 commit remain historical evidence.";
        reference_source.worklog.push_back(
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9998 -> GT-TSK-9999\r");
        reference_source.worklog.push_back(
            "2026-03-12 00:02 [agent=opencode] Remapped ID: GT-TSK-9999 -> GT-TSK-0001");
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
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-TSK-9999") == extracted_refs.end(),
            "ordered two-hop Worklog remap sources should remain historical prose");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-TSK-0001") != extracted_refs.end(),
            "the current item ID should never be suppressed from prose");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-FTR-9998") != extracted_refs.end() &&
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-FTR-0002") != extracted_refs.end(),
            "canonical-remap wording without Worklog proof should remain fail-closed");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-9998") != extracted_refs.end() &&
               std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-9997") != extracted_refs.end(),
            "legacy and mislabeled wording without Worklog proof should remain fail-closed");
        expect(std::find(extracted_refs.begin(), extracted_refs.end(), "GT-BUG-0004") != extracted_refs.end(),
            "unrelated active prose references should remain validated");
        expect(std::none_of(extracted_refs.begin(), extracted_refs.end(), [](const std::string& ref) {
            return ref.find("Evidence sentence") != std::string::npos;
        }), "decision prose should not become a whole path reference");

        BacklogItem one_hop_reference = item;
        one_hop_reference.decisions = {
            "Historical identity GT-TSK-9996 now belongs to current item GT-TSK-0001."
        };
        one_hop_reference.worklog.push_back(
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9996 -> GT-TSK-0001\r");
        const auto one_hop_refs = RefResolver::get_references(one_hop_reference);
        expect(std::find(one_hop_refs.begin(), one_hop_refs.end(), "GT-TSK-9996") == one_hop_refs.end(),
            "an exact one-hop Worklog remap source should remain historical prose");
        expect(std::find(one_hop_refs.begin(), one_hop_refs.end(), "GT-TSK-0001") != one_hop_refs.end(),
            "an exact one-hop Worklog remap target should remain active prose");

        BacklogItem structured_alias_reference = one_hop_reference;
        structured_alias_reference.links.relates = {"GT-TSK-9996"};
        const auto structured_alias_refs = RefResolver::get_references(structured_alias_reference);
        expect(std::find(structured_alias_refs.begin(), structured_alias_refs.end(), "GT-TSK-9996") != structured_alias_refs.end(),
            "structured links should remain unconditional even for a proven historical prose alias");

        const auto expect_unproven_alias_retained = [&](const std::vector<std::string>& worklog, const std::string& message) {
            BacklogItem candidate = item;
            candidate.decisions = {"Historical identity GT-TSK-9995 remains reviewable."};
            candidate.worklog = worklog;
            const auto candidate_refs = RefResolver::get_references(candidate);
            expect(std::find(candidate_refs.begin(), candidate_refs.end(), "GT-TSK-9995") != candidate_refs.end(), message);
        };
        expect_unproven_alias_retained({
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-0001."
        }, "a malformed remap message should fail closed");
        expect_unproven_alias_retained({
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-9994",
            "2026-03-12 00:02 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-0001"
        }, "a branched remap history should fail closed");
        expect_unproven_alias_retained({
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-9994",
            "2026-03-12 00:02 [agent=opencode] Remapped ID: GT-TSK-9994 -> GT-TSK-9995",
            "2026-03-12 00:03 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-0001"
        }, "a cyclic remap history should fail closed");
        expect_unproven_alias_retained({
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9994 -> GT-TSK-0001",
            "2026-03-12 00:02 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-9994"
        }, "an out-of-order remap history should fail closed");
        expect_unproven_alias_retained({
            "2026-03-12 00:01 [agent=opencode] Remapped ID: GT-TSK-9995 -> GT-TSK-9994"
        }, "a remap chain ending at an unrelated ID should fail closed");

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

        expect(to_string(ProductResolutionKind::CanonicalSlug) == "canonical_slug",
            "canonical product resolution kind string must remain stable");
        expect(to_string(ProductResolutionKind::Prefix) == "prefix",
            "prefix product resolution kind string must remain stable");
        expect(to_string(ProductResolutionKind::DisplayName) == "display_name",
            "display-name product resolution kind string must remain stable");
        expect(to_string(ProductResolutionKind::RepoBinding) == "repo_binding",
            "repo-binding product resolution kind string must remain stable");
        expect(to_string(ProductResolutionKind::ExplicitAlias) == "explicit_alias",
            "explicit-alias product resolution kind string must remain stable");
        const std::string utf8_upper_a_umlaut = "\xC3\x84" "BC";
        expect(ProjectConfig::normalize_product_selector(" \t" + utf8_upper_a_umlaut + "\r ") ==
                "\xC3\x84" "bc",
            "selector normalization should lowercase ASCII while preserving non-ASCII UTF-8 bytes");
        expect(ProjectConfig::normalize_product_selector(utf8_upper_a_umlaut) != "\xC3\xA4" "bc",
            "selector normalization must not perform Unicode case folding");

        {
            const DisposableDirectory resolver_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-product-resolver-smoke");
            const auto config_path = resolver_fixtures.path() / ".kano" / "backlog_config.toml";
            write_text(
                config_path,
                "[products.horizon-rpg]\n"
                "name = \"HorizonRPG\"\n"
                "prefix = \"HRR\"\n"
                "backlog_root = \"_kano/backlog/products/horizon-rpg\"\n"
                "aliases = [\"horizon\", \"HorizonRPG\", \"HRR\", \"horizon-rpg\", \"shared-selector\"]\n"
                "repo_bindings = [\"horizon-rpg-plugin\", \"HorizonRPG\", \"HRR\", \"shared-selector\"]\n"
                "default_assignee = \"agent-default\"\n"
                "default_bug_reviewer = \"review-default\"\n\n"
                "[products.tooling]\n"
                "name = \"Tooling\"\n"
                "prefix = \"TLG\"\n"
                "backlog_root = \"_kano/backlog/products/tooling\"\n");
            write_text(
                resolver_fixtures.path() / "_kano" / "backlog" / "products" /
                    "horizon-rpg" / "_config" / "config.toml",
                "[product]\n"
                "aliases = [\"local-horizon\"]\n"
                "repo_bindings = [\"local-horizon-repo\"]\n"
                "default_assignee = \"koa\"\n"
                "default_bug_reviewer = \"reviewer-koa\"\n");

            const auto project_config = ProjectConfig::load_from_toml(config_path);
            expect(project_config.has_value(), "rich project config should parse");
            const auto& horizon = project_config->products.at("horizon-rpg");
            expect(horizon.aliases.size() == 5,
                "project config should preserve every explicit product alias");
            expect(horizon.repo_bindings.size() == 4,
                "project config should preserve every product repo binding");
            expect(project_config->products.at("tooling").aliases.empty() &&
                    project_config->products.at("tooling").repo_bindings.empty(),
                "missing selector arrays must remain empty");

            expect_product_resolution(*project_config, "horizon-rpg", "horizon-rpg",
                ProductResolutionKind::CanonicalSlug, "horizon-rpg");
            expect_product_resolution(*project_config, " \tHoRiZoN-RpG\r ", "horizon-rpg",
                ProductResolutionKind::CanonicalSlug, "horizon-rpg");
            expect_product_resolution(*project_config, "HorizonRPG", "horizon-rpg",
                ProductResolutionKind::DisplayName, "HorizonRPG");
            expect_product_resolution(*project_config, "\tHoRiZoNrPg\r", "horizon-rpg",
                ProductResolutionKind::DisplayName, "HorizonRPG");
            expect_product_resolution(*project_config, "HRR", "horizon-rpg",
                ProductResolutionKind::Prefix, "HRR");
            expect_product_resolution(*project_config, "\v hrr \f", "horizon-rpg",
                ProductResolutionKind::Prefix, "HRR");
            expect_product_resolution(*project_config, "horizon-rpg-plugin", "horizon-rpg",
                ProductResolutionKind::RepoBinding, "horizon-rpg-plugin");
            expect_product_resolution(*project_config, "shared-selector", "horizon-rpg",
                ProductResolutionKind::RepoBinding, "shared-selector");
            expect_product_resolution(*project_config, "\vHoRiZoN\f", "horizon-rpg",
                ProductResolutionKind::ExplicitAlias, "horizon");
            expect(project_config->resolve_product_name(" horizon ").value_or("") == "horizon-rpg",
                "resolve_product_name should project the rich resolution to the canonical slug");
            expect(!project_config->resolve_product("horizonrpgplugin").has_value(),
                "selector normalization must not remove separators");

            const auto claims = project_config->selector_claims();
            const auto hrr_claim = std::find_if(claims.begin(), claims.end(), [](const auto& claim) {
                return claim.canonical_slug == "horizon-rpg" && claim.normalized_selector == "hrr";
            });
            expect(hrr_claim != claims.end() &&
                    hrr_claim->resolution_kind == ProductResolutionKind::Prefix &&
                    hrr_claim->selector == "HRR",
                "prefix must win over same-product repo and explicit-alias claims");
            const auto display_claim = std::find_if(claims.begin(), claims.end(), [](const auto& claim) {
                return claim.canonical_slug == "horizon-rpg" && claim.normalized_selector == "horizonrpg";
            });
            expect(display_claim != claims.end() &&
                    display_claim->resolution_kind == ProductResolutionKind::DisplayName,
                "display name must win over same-product repo and explicit-alias claims");
            const auto shared_claim = std::find_if(claims.begin(), claims.end(), [](const auto& claim) {
                return claim.canonical_slug == "horizon-rpg" &&
                    claim.normalized_selector == "shared-selector";
            });
            expect(shared_claim != claims.end() &&
                    shared_claim->resolution_kind == ProductResolutionKind::RepoBinding,
                "repo binding must win over a same-product explicit alias claim");
            expect(project_config->find_selector_collisions().empty(),
                "same-product selector overlap should collapse by precedence instead of colliding");

            const auto canonical_root = project_config->resolve_backlog_root("horizon-rpg", config_path);
            const auto alias_root = project_config->resolve_backlog_root("horizon", config_path);
            expect(canonical_root.has_value() && alias_root == canonical_root,
                "resolve_backlog_root should delegate aliases through rich product resolution");

            const std::string requested_alias = "\tHORIZON\r";
            const auto alias_context = BacklogContext::resolve(
                resolver_fixtures.path(), requested_alias, std::nullopt);
            expect(alias_context.product_name == "horizon-rpg",
                "BacklogContext should preserve the canonical product slug");
            expect(alias_context.product_resolution.canonical_slug == alias_context.product_name &&
                    alias_context.product_resolution.resolution_kind == ProductResolutionKind::ExplicitAlias &&
                    alias_context.product_resolution.requested_selector == requested_alias &&
                    alias_context.product_resolution.normalized_selector == "horizon" &&
                    alias_context.product_resolution.matched_selector == "horizon",
                "BacklogContext should retain complete product resolution metadata");
            expect(alias_context.product_def.default_assignee.value_or("") == "koa" &&
                    alias_context.product_def.default_bug_reviewer.value_or("") == "reviewer-koa",
                "product-local config should preserve ordinary field overrides");
            expect(alias_context.product_def.aliases == horizon.aliases &&
                    alias_context.product_def.repo_bindings == horizon.repo_bindings,
                "product-local config must not override project selector arrays");

            std::string local_selector_diagnostic;
            try {
                static_cast<void>(BacklogContext::resolve(
                    resolver_fixtures.path(), std::string("local-horizon"), std::nullopt));
            } catch (const kano::backlog_core::ConfigError& error) {
                local_selector_diagnostic = error.what();
            }
            expect(local_selector_diagnostic.find("not found") != std::string::npos,
                "product-local selector arrays must not join the project resolver surface");
        }

        {
            const DisposableDirectory malformed_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-malformed-selector-arrays-smoke");
            const std::vector<std::string> malformed_values = {
                "aliases = [horizon]\n",
                "repo_bindings = [\"horizon-rpg-plugin\",]\n",
                "aliases = [\n  \"horizon\"\n]\n",
                "aliases = [\"bad\\qescape\"]\n",
            };
            for (std::size_t index = 0; index < malformed_values.size(); ++index) {
                const auto config_path = malformed_fixtures.path() /
                    ("malformed-" + std::to_string(index) + ".toml");
                write_text(
                    config_path,
                    "[products.horizon-rpg]\n"
                    "name = \"HorizonRPG\"\n"
                    "prefix = \"HRR\"\n"
                    "backlog_root = \"_kano/backlog/products/horizon-rpg\"\n" +
                    malformed_values[index]);
                std::string diagnostic;
                try {
                    static_cast<void>(ProjectConfig::load_from_toml(config_path));
                } catch (const kano::backlog_core::ConfigError& error) {
                    diagnostic = error.what();
                }
                expect(diagnostic.find("Invalid TOML string array") != std::string::npos,
                    "malformed project selector arrays must fail closed");
            }
        }

        {
            const DisposableDirectory empty_key_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-empty-product-key-smoke");
            const std::vector<std::string> product_sections = {
                "[products.\"\"]\n",
                "[products.\"   \"]\n",
            };
            for (std::size_t index = 0; index < product_sections.size(); ++index) {
                const auto config_path = empty_key_fixtures.path() /
                    ("empty-product-key-" + std::to_string(index) + ".toml");
                write_text(config_path,
                    product_sections[index] +
                    "name = \"Invalid Product\"\n"
                    "prefix = \"INV\"\n"
                    "backlog_root = \"_kano/backlog/products/invalid\"\n");
                std::string diagnostic;
                try {
                    static_cast<void>(ProjectConfig::load_from_toml(config_path));
                } catch (const kano::backlog_core::ConfigError& error) {
                    diagnostic = error.what();
                }
                expect(diagnostic == "Canonical product selector cannot normalize to empty",
                    "empty and whitespace canonical product keys must fail with a stable diagnostic");
            }
        }

        {
            const DisposableDirectory collision_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-selector-collision-smoke");
            const auto config_path = collision_fixtures.path() / ".kano" / "backlog_config.toml";
            write_text(
                config_path,
                 "[products.horizon-rpg]\n"
                 "name = \"HorizonRPG\"\n"
                 "prefix = \"ALP\"\n"
                 "backlog_root = \"_kano/backlog/products/horizon-rpg\"\n"
                 "aliases = [\"horizon\"]\n"
                 "repo_bindings = [\"horizon-rpg-plugin\"]\n\n"
                 "[products.alp]\n"
                 "name = \"Registry Repair\"\n"
                 "prefix = \"RPR\"\n"
                 "backlog_root = \"_kano/backlog/products/alp\"\n"
                 "aliases = [\"horizon-rpg-plugin\"]\n\n"
                "[products.safe-tools]\n"
                "name = \"Safe Tools\"\n"
                "prefix = \"SAFE\"\n"
                "backlog_root = \"_kano/backlog/products/safe-tools\"\n");

            const auto collision_config = ProjectConfig::load_from_toml(config_path);
            expect(collision_config.has_value(), "selector collision config should parse");
            const auto collisions = collision_config->find_selector_collisions();
            expect(collisions.size() == 2,
                "cross-product claims should report every collided normalized selector");
             const auto alp_collision = std::find_if(
                 collisions.begin(), collisions.end(), [](const auto& collision) {
                     return collision.normalized_selector == "alp";
                 });
            const auto repo_collision = std::find_if(
                collisions.begin(), collisions.end(), [](const auto& collision) {
                    return collision.normalized_selector == "horizon-rpg-plugin";
                });
             expect(alp_collision != collisions.end() && alp_collision->claims.size() == 2,
                 "a canonical slug colliding with another product prefix should be diagnosed");
            expect(repo_collision != collisions.end() && repo_collision->claims.size() == 2,
                "repo-binding and explicit-alias ownership should collide across products");

            const auto collision_diagnostic = ProjectConfig::describe_selector_collisions(collisions);
            expect(collision_diagnostic == ProjectConfig::describe_selector_collisions(collisions),
                "selector collision diagnostics should be deterministic");
             expect(collision_diagnostic.find("horizon-rpg") != std::string::npos &&
                     collision_diagnostic.find("alp") != std::string::npos &&
                     collision_diagnostic.find("repo_binding") != std::string::npos &&
                    collision_diagnostic.find("explicit_alias") != std::string::npos,
                "selector collision diagnostics should identify canonical owners and stable claim kinds");

            expect_product_resolution(*collision_config, "horizon-rpg", "horizon-rpg",
                ProductResolutionKind::CanonicalSlug, "horizon-rpg");
            for (const std::string selector : {"alp", "ALP", " alp ", "horizon-rpg-plugin"}) {
                std::string diagnostic;
                try {
                    static_cast<void>(collision_config->resolve_product(selector));
                } catch (const kano::backlog_core::ConfigError& error) {
                    diagnostic = error.what();
                }
                expect(diagnostic.find("Product selector collision") != std::string::npos,
                    "cross-product selector collisions must fail closed for every input casing");
            }

            const auto exact_context = BacklogContext::resolve(
                collision_fixtures.path(), std::string("horizon-rpg"), std::nullopt);
            expect(exact_context.product_name == "horizon-rpg" &&
                    exact_context.product_resolution.resolution_kind ==
                        ProductResolutionKind::CanonicalSlug,
                "BacklogContext must preserve unambiguous canonical lookup");

            std::string context_collision_diagnostic;
            try {
                static_cast<void>(BacklogContext::resolve(
                    collision_fixtures.path(), std::string("alp"), std::nullopt));
            } catch (const kano::backlog_core::ConfigError& error) {
                context_collision_diagnostic = error.what();
            }
            expect(context_collision_diagnostic.find("Product selector collision") != std::string::npos,
                "BacklogContext must reject an exact canonical token that is ambiguous after normalization");

            const auto repair_context = BacklogContext::resolve(
                collision_fixtures.path(), std::string("safe-tools"), std::nullopt);
            expect(repair_context.product_name == "safe-tools" &&
                    repair_context.product_resolution.canonical_slug == "safe-tools" &&
                    repair_context.product_resolution.resolution_kind == ProductResolutionKind::CanonicalSlug,
                "an unrelated canonical product must remain available for registry repair");
            expect_product_resolution(*collision_config, "RPR", "alp",
                ProductResolutionKind::Prefix, "RPR");
            const auto collided_product_repair_context = BacklogContext::resolve(
                collision_fixtures.path(), std::string("RPR"), std::nullopt);
            expect(collided_product_repair_context.product_name == "alp" &&
                    collided_product_repair_context.product_resolution.resolution_kind ==
                        ProductResolutionKind::Prefix,
                "a collided product must remain reachable through another unique configured selector");
        }

        {
            const DisposableDirectory reload_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-selector-reload-smoke");
            const auto config_path = reload_fixtures.path() / ".kano" / "backlog_config.toml";
            write_text(
                config_path,
                "[products.horizon-rpg]\n"
                "name = \"HorizonRPG\"\n"
                "prefix = \"HRR\"\n"
                "backlog_root = \"_kano/backlog/products/horizon-rpg\"\n"
                "aliases = [\"horizon-old\"]\n");
            const auto original_config = ProjectConfig::load_from_toml(config_path);
            expect(original_config.has_value(), "original reload fixture should parse");
            expect_product_resolution(*original_config, "horizon-old", "horizon-rpg",
                ProductResolutionKind::ExplicitAlias, "horizon-old");

            write_text(
                config_path,
                "[products.horizon-rpg]\n"
                "name = \"Horizon Reloaded\"\n"
                "prefix = \"HR2\"\n"
                "backlog_root = \"_kano/backlog/products/horizon-rpg\"\n"
                "aliases = [\"horizon-new\"]\n");
            expect(original_config->resolve_product("horizon-new") == std::nullopt &&
                    original_config->resolve_product("horizon-old").has_value(),
                "an already loaded config snapshot should not drift after the file changes");

            const auto reloaded_config = ProjectConfig::load_from_toml(config_path);
            expect(reloaded_config.has_value() &&
                    !reloaded_config->resolve_product("horizon-old").has_value(),
                "reloading should replace removed selector claims instead of accumulating them");
            expect_product_resolution(*reloaded_config, "horizon-new", "horizon-rpg",
                ProductResolutionKind::ExplicitAlias, "horizon-new");
            expect_product_resolution(*reloaded_config, "horizon-rpg", "horizon-rpg",
                ProductResolutionKind::CanonicalSlug, "horizon-rpg");

            const auto reloaded_context = BacklogContext::resolve(
                reload_fixtures.path(), std::string("horizon-new"), std::nullopt);
            expect(reloaded_context.product_name == "horizon-rpg" &&
                    reloaded_context.product_resolution.canonical_slug == "horizon-rpg" &&
                    reloaded_context.product_resolution.matched_selector == "horizon-new",
                "BacklogContext should reload selector metadata while preserving the canonical slug");
        }

        {
            const DisposableDirectory prefix_collision_fixtures(
                std::filesystem::temp_directory_path() /
                    "kano-backlog-core-prefix-canonical-owner-smoke");
            const auto config_path =
                prefix_collision_fixtures.path() / ".kano" / "backlog_config.toml";
            write_text(
                config_path,
                "[products.canonical-owner]\n"
                "name = \"Canonical Owner\"\n"
                "prefix = \"DUP\"\n"
                "backlog_root = \"_kano/backlog/products/canonical-owner\"\n\n"
                "[products.peer]\n"
                "name = \"Peer\"\n"
                "prefix = \"DUP\"\n"
                "backlog_root = \"_kano/backlog/products/peer\"\n"
                "aliases = [\"canonical-owner\"]\n");

            const auto config = ProjectConfig::load_from_toml(config_path);
            expect(config.has_value(), "prefix collision canonical-owner fixture should parse");
            const auto prefix_collisions = config->find_prefix_collisions(config_path);
            expect(prefix_collisions.size() == 1,
                "prefix collision discovery should not publicly re-resolve canonical map keys");
            expect(prefix_collisions.front().left_product == "canonical-owner" &&
                    prefix_collisions.front().right_product == "peer" &&
                    prefix_collisions.front().left_backlog_root.find("canonical-owner") !=
                        std::string::npos &&
                    prefix_collisions.front().right_backlog_root.find("peer") !=
                        std::string::npos,
                "prefix collision diagnostics should retain roots for colliding canonical owners");
        }

        {
            const DisposableDirectory legacy_fixtures(
                std::filesystem::temp_directory_path() / "kano-backlog-core-legacy-config-smoke");
            const auto config_path = legacy_fixtures.path() / ".kano" / "backlog_config.toml";
            write_text(
                config_path,
                "[products.legacy]\n"
                "name = \"Legacy Product\"\n"
                "prefix = \"LEG\"\n"
                "backlog_root = \"_kano/backlog/products/legacy\"\n"
                "default_assignee = \"legacy-default\"\n");
            write_text(
                legacy_fixtures.path() / "_kano" / "backlog" / "products" /
                    "legacy" / "_config" / "config.toml",
                "[product]\n"
                "default_assignee = \"legacy-local\"\n");

            const auto legacy_config = ProjectConfig::load_from_toml(config_path);
            expect(legacy_config.has_value(), "legacy config without selector arrays should parse");
            expect(legacy_config->products.at("legacy").aliases.empty() &&
                    legacy_config->products.at("legacy").repo_bindings.empty(),
                "legacy configs should default missing selector arrays to empty");
            expect_product_resolution(*legacy_config, " Legacy Product ", "legacy",
                ProductResolutionKind::DisplayName, "Legacy Product");
            expect_product_resolution(*legacy_config, " LEG ", "legacy",
                ProductResolutionKind::Prefix, "LEG");

            const auto legacy_context = BacklogContext::resolve(
                legacy_fixtures.path(), std::string(" LEG "), std::nullopt);
            expect(legacy_context.product_name == "legacy" &&
                    legacy_context.product_resolution.resolution_kind == ProductResolutionKind::Prefix &&
                    legacy_context.product_def.default_assignee.value_or("") == "legacy-local",
                "legacy configs should retain prefix resolution and product-local field overrides");
            const auto implicit_legacy_context = BacklogContext::resolve(
                legacy_fixtures.path(), std::nullopt, std::nullopt);
            expect(implicit_legacy_context.product_name == "legacy" &&
                    implicit_legacy_context.product_resolution.resolution_kind ==
                        ProductResolutionKind::CanonicalSlug &&
                    implicit_legacy_context.product_resolution.requested_selector == "legacy" &&
                    implicit_legacy_context.product_resolution.matched_selector == "legacy",
                "single-product context should construct canonical resolution without public parsing");
        }

        std::cout << "backlog_core_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "backlog_core_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
