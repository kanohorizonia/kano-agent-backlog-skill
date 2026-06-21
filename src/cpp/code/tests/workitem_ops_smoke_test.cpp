#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>

#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/workitem/workitem_ops.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_root() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> dist(0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << dist(gen);

    std::filesystem::path root = std::filesystem::temp_directory_path() /
        "kano-backlog-workitem-smoke" /
        suffix.str();
    std::filesystem::create_directories(root / "items");
    std::filesystem::create_directories(root / "views");
    std::filesystem::create_directories(root / "_meta");
    return root;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void set_ready_fields(kano::backlog_core::BacklogItem& item) {
    item.context = "Need deterministic parent sync coverage.";
    item.goal = "Update a child item without trusting stale indexed host paths.";
    item.approach = "Use isolated temp roots and a deliberately stale parent index row.";
    item.acceptance_criteria = "Child state update syncs the real parent file.";
    item.risks = "Low risk - isolated smoke fixture.";
}

bool diagnostics_contain(
    const kano::backlog_core::UpdateStateResult& result,
    const std::string& needle
) {
    for (const auto& diagnostic : result.intent_diagnostics) {
        if (diagnostic.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void expect_diagnostic_contains(
    const kano::backlog_core::UpdateStateResult& result,
    const std::string& needle,
    const std::string& message
) {
    expect(diagnostics_contain(result, needle), message);
}

void expect_no_diagnostic_contains(
    const kano::backlog_core::UpdateStateResult& result,
    const std::string& needle,
    const std::string& message
) {
    expect(!diagnostics_contain(result, needle), message);
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;

    std::filesystem::path root;
    std::filesystem::path external_root;

    try {
        root = make_temp_root();
        {
            BacklogIndex index(root / ".cache" / "index" / "backlog.db");
            index.initialize();

            auto created = WorkitemOps::create_item(index, root, "TST", ItemType::Task, "Native workitem smoke", "opencode");
            expect(created.id.rfind("TST-TSK-", 0) == 0, "created id should use task prefix");

            auto loaded_path = index.get_path_by_id(created.id);
            expect(loaded_path.has_value(), "created item should be indexed");
            const auto created_text = read_text(created.path);
            expect(created_text.find("# Non-Goals / Do Not") != std::string::npos, "created item template should include Non-Goals / Do Not");
            expect(created_text.find("# Intent Amendments") != std::string::npos, "created item template should include Intent Amendments");

            auto queried = index.query_items(ItemType::Task, std::nullopt);
            expect(!queried.empty(), "task item should appear in index query");

            CanonicalStore store(root);

            auto issue_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Issue,
                "Unclear runtime gap smoke",
                "opencode");
            expect(issue_created.id.rfind("TST-ISS-", 0) == 0, "created id should use issue prefix");
            expect(
                issue_created.path.parent_path().parent_path().filename().string() == "issue",
                "created issue should be stored under items/issue");

            auto issue_queried = index.query_items(ItemType::Issue, std::nullopt);
            expect(!issue_queried.empty(), "issue item should appear in index query");

            auto issue_item = store.read(issue_created.path);
            expect(issue_item.type == ItemType::Issue, "created issue should round-trip with Issue type");
            set_ready_fields(issue_item);
            issue_item.context = "Need pre-triage capture for an unclear runtime gap.";
            issue_item.goal = "Preserve blocker and risk context before choosing task or bug remediation.";
            issue_item.risks = "Incorrect type selection could hide unresolved blocker evidence.";
            store.write(issue_item);
            index.index_item(issue_item);

            auto issue_update = WorkitemOps::update_state(
                index,
                root,
                issue_created.id,
                ItemState::InProgress,
                "opencode",
                std::string("Issue triage started"));
            expect(issue_update.worklog_appended, "issue state update should append worklog");
            auto issue_after_update = store.read(issue_created.path);
            expect(issue_after_update.state == ItemState::InProgress, "issue should transition to InProgress");
            expect(issue_after_update.worklog.back().find("Issue triage started") != std::string::npos, "issue worklog should preserve update message");
            {
                BacklogIndex reopened_index(root / ".cache" / "index" / "backlog.db");
                auto issue_after_reopen = reopened_index.query_items(ItemType::Issue, std::nullopt);
                expect(issue_after_reopen.size() == 1, "issue query should survive update-state and DB reopen");
                expect(issue_after_reopen.front().id == issue_created.id, "issue query should return the updated issue");
                expect(issue_after_reopen.front().state == ItemState::InProgress, "issue query should preserve updated state");
            }

            auto parent_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Feature,
                "Provider parent sync smoke",
                "opencode");
            auto parent_item = store.read(parent_created.path);
            set_ready_fields(parent_item);
            store.write(parent_item);
            index.index_item(parent_item);

            auto child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Provider child state smoke",
                "opencode",
                parent_created.id);
            auto child_item = store.read(child_created.path);
            set_ready_fields(child_item);
            store.write(child_item);
            index.index_item(child_item);

            auto epic_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Epic,
                "Intent stack epic smoke",
                "opencode");
            auto epic_item = store.read(epic_created.path);
            epic_item.context = "Epic inherited context.";
            epic_item.goal = "Epic inherited goal.";
            epic_item.non_goals = "Do not override epic boundaries.";
            epic_item.intent_amendments = "2026-06-20: Epic amendment.";
            store.write(epic_item);
            index.index_item(epic_item);

            auto feature_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Feature,
                "Intent stack feature smoke",
                "opencode",
                epic_created.id);
            auto feature_item = store.read(feature_created.path);
            feature_item.context = "Feature inherited context.";
            feature_item.goal = "Feature inherited goal.";
            feature_item.acceptance_criteria = "Feature acceptance.";
            feature_item.non_goals = "Do not skip feature scope.";
            store.write(feature_item);
            index.index_item(feature_item);

            auto story_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::UserStory,
                "Intent stack story smoke",
                "opencode",
                feature_created.id);
            auto story_item = store.read(story_created.path);
            story_item.context = "Story inherited context.";
            story_item.goal = "Story inherited goal.";
            story_item.acceptance_criteria = "Story acceptance.";
            story_item.intent_amendments = "2026-06-20: Story amendment.";
            store.write(story_item);
            index.index_item(story_item);

            auto stack_task_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Intent stack task smoke",
                "opencode",
                story_created.id);
            auto stack_task = store.read(stack_task_created.path);
            set_ready_fields(stack_task);
            stack_task.non_goals = "Do not mutate while resolving.";
            store.write(stack_task);
            index.index_item(stack_task);

            auto intent_stack = WorkitemOps::resolve_intent_stack(root, stack_task_created.id);
            expect(intent_stack.chain.size() == 4, "intent stack should resolve task-story-feature-epic chain");
            expect(intent_stack.chain[0].item.id == stack_task_created.id, "intent stack should start at current task");
            expect(intent_stack.chain[1].item.id == story_created.id, "intent stack should include parent story");
            expect(intent_stack.chain[2].item.id == feature_created.id, "intent stack should include parent feature");
            expect(intent_stack.chain[3].item.id == epic_created.id, "intent stack should include parent epic");
            expect(intent_stack.chain[0].role == "task", "intent stack current role should be task");
            expect(intent_stack.chain[1].role == "story", "intent stack parent role should be story");
            expect(intent_stack.warnings.empty(), "complete intent stack should not emit warnings");
            expect(intent_stack.chain[3].item.non_goals && intent_stack.chain[3].item.non_goals->find("epic boundaries") != std::string::npos,
                "intent stack should preserve ancestor non-goals");
            expect(intent_stack.chain[1].item.intent_amendments && intent_stack.chain[1].item.intent_amendments->find("Story amendment") != std::string::npos,
                "intent stack should preserve ancestor intent amendments");

            auto orphan_stack = WorkitemOps::resolve_intent_stack(root, created.id);
            expect(orphan_stack.chain.size() == 1, "orphan item stack should contain only the current item");
            expect(orphan_stack.warnings.empty(), "orphan item stack should not warn");

            const auto create_review_task = [&](const std::string& title,
                                                const std::vector<std::string>& worklog,
                                                std::optional<std::string> amendments = std::nullopt) {
                auto convergence_created = WorkitemOps::create_item(
                    index,
                    root,
                    "TST",
                    ItemType::Task,
                    title,
                    "opencode");
                auto convergence_item = store.read(convergence_created.path);
                set_ready_fields(convergence_item);
                convergence_item.state = ItemState::Review;
                convergence_item.worklog = worklog;
                convergence_item.intent_amendments = amendments;
                store.write(convergence_item);
                index.index_item(convergence_item);
                return convergence_created.id;
            };

            const auto close_review_task = [&](const std::string& item_id) {
                return WorkitemOps::update_state(
                    index,
                    root,
                    item_id,
                    ItemState::Done,
                    "opencode",
                    std::string("Done branch convergence smoke"));
            };

            const auto missing_convergence_id = create_review_task(
                "Missing branch convergence evidence smoke",
                {"Implementation finished; validation passed without branch convergence keys."});
            auto missing_convergence_done = close_review_task(missing_convergence_id);
            expect_diagnostic_contains(
                missing_convergence_done,
                "missing target branch evidence",
                "missing branch convergence evidence should warn about target/default target evidence");

            const auto default_target_id = create_review_task(
                "Default target convergence evidence smoke",
                {"Branch convergence: target=repo-default; implementation_commit=abc1234; reachable_from_target=true; remote_publication=origin/main"});
            auto default_target_done = close_review_task(default_target_id);
            expect_no_diagnostic_contains(
                default_target_done,
                "Review->Done branch convergence",
                "complete default target convergence evidence should not warn");

            const auto explicit_target_id = create_review_task(
                "Explicit target convergence evidence smoke",
                {"Branch convergence: target=release/2026.06; implementation_commit=def5678; reachable_from_target=yes; remote_publication=origin/release/2026.06"});
            auto explicit_target_done = close_review_task(explicit_target_id);
            expect_no_diagnostic_contains(
                explicit_target_done,
                "Review->Done branch convergence",
                "complete explicit target convergence evidence should not warn");

            const auto side_branch_id = create_review_task(
                "Side branch delivery without human choice smoke",
                {"Branch convergence: target=feature/side-only; implementation_commit=f00d123; reachable_from_target=true; remote_publication=origin/feature/side-only; side_branch_delivery=agent-choice"});
            auto side_branch_done = close_review_task(side_branch_id);
            expect_diagnostic_contains(
                side_branch_done,
                "side-branch delivery lacks explicit human choice",
                "side-branch-only delivery without human approval should warn");

            const auto unpublished_id = create_review_task(
                "Unpublished target convergence smoke",
                {"Branch convergence: target=main; implementation_commit=badcafe; reachable_from_target=true"});
            auto unpublished_done = close_review_task(unpublished_id);
            expect_diagnostic_contains(
                unpublished_done,
                "missing remote_publication",
                "missing target branch remote publication evidence should warn");

            const auto nested_missing_id = create_review_task(
                "Nested gitlink missing evidence smoke",
                {"Submodule update affected nested work. Branch convergence: target=main; implementation_commit=123abcd; reachable_from_target=true; remote_publication=origin/main"});
            auto nested_missing_done = close_review_task(nested_missing_id);
            expect_diagnostic_contains(
                nested_missing_done,
                "without nested_gitlink evidence",
                "nested/submodule marker without parent gitlink evidence should warn");

            const auto nested_complete_id = create_review_task(
                "Nested gitlink complete evidence smoke",
                {"Submodule update affected nested work. Branch convergence: target=main; implementation_commit=456abcd; reachable_from_target=true; remote_publication=origin/main; nested_gitlink=parent-pointer-updated"});
            auto nested_complete_done = close_review_task(nested_complete_id);
            expect_no_diagnostic_contains(
                nested_complete_done,
                "without nested_gitlink evidence",
                "nested/submodule work with nested_gitlink evidence should not warn");

            const auto blocked_convergence_id = create_review_task(
                "Blocked convergence advisory smoke",
                {"Blocked convergence: branch=feature/stuck; reason=target conflict; next=ask maintainer to choose target; blocker=KOB-TSK-0099"});
            auto blocked_convergence_done = close_review_task(blocked_convergence_id);
            expect(blocked_convergence_done.new_state == ItemState::Done, "blocked convergence warning should remain advisory only");
            expect_diagnostic_contains(
                blocked_convergence_done,
                "blocked convergence recorded; item should remain not Done",
                "blocked convergence evidence should warn that the item should remain not Done");
            expect_diagnostic_contains(
                blocked_convergence_done,
                "branch=feature/stuck; blocker=KOB-TSK-0099; reason=target conflict; next=ask maintainer to choose target",
                "blocked convergence diagnostic should retain branch, blocker, reason, and next step");

            auto missing_parent_task = stack_task;
            missing_parent_task.id = "TST-TSK-9997";
            missing_parent_task.uid = "019cdf6a-0000-7000-8000-000000009997";
            missing_parent_task.parent = "TST-FTR-9997";
            missing_parent_task.file_path = root / "items" / "task" / "0000" / "TST-TSK-9997_missing-parent.md";
            store.write(missing_parent_task);
            index.index_item(missing_parent_task);
            auto missing_stack = WorkitemOps::resolve_intent_stack(root, missing_parent_task.id);
            expect(missing_stack.chain.size() == 1, "missing-parent stack should keep resolved current item");
            expect(!missing_stack.warnings.empty(), "missing-parent stack should warn");
            expect(missing_stack.warnings.front().find("could not be resolved") != std::string::npos, "missing-parent warning should be explicit");

            auto path_parent_task = stack_task;
            path_parent_task.id = "TST-TSK-9996";
            path_parent_task.uid = "019cdf6a-0000-7000-8000-000000009996";
            const auto path_like_parent_ref = (root.parent_path() / "outside-root" / "items" / "feature" / "0000" / "outside.md").string();
            path_parent_task.parent = path_like_parent_ref;
            path_parent_task.file_path = root / "items" / "task" / "0000" / "TST-TSK-9996_path-parent.md";
            store.write(path_parent_task);
            index.index_item(path_parent_task);
            auto path_stack = WorkitemOps::resolve_intent_stack(root, path_parent_task.id);
            expect(path_stack.chain.size() == 1, "path-like parent stack should keep resolved current item");
            expect(!path_stack.warnings.empty(), "path-like parent stack should warn");
            expect(path_stack.warnings.front().find("path-like") != std::string::npos, "path-like parent warning should be explicit");
            expect(path_stack.warnings.front().find(path_like_parent_ref) == std::string::npos, "path-like parent warning should redact path");

            auto bounded_stack = WorkitemOps::resolve_intent_stack(root, stack_task_created.id, 2);
            expect(bounded_stack.chain.size() == 2, "bounded intent stack should stop at max depth");
            expect(!bounded_stack.warnings.empty(), "bounded intent stack should warn at depth limit");

            auto stale_parent_item = parent_item;
            stale_parent_item.file_path = std::filesystem::temp_directory_path() /
                "kano-backlog-stale-host-root" /
                parent_created.path.filename();
            index.index_item(stale_parent_item);

            auto update_result = WorkitemOps::update_state(
                index,
                root,
                child_created.id,
                ItemState::InProgress,
                "opencode");
            expect(update_result.parent_synced, "parent should sync despite stale indexed path");

            auto synced_parent = store.read(parent_created.path);
            expect(synced_parent.state == ItemState::InProgress, "real parent should be synced to InProgress");

            auto synced_child = store.read(child_created.path);
            expect(synced_child.state == ItemState::InProgress, "child should be updated to InProgress");

            // Shared-layout regression: CLI state updates use a global backlog
            // index under _kano/backlog while mutations target a product root.
            // Parent sync must not trust a stale host-only path from that index.
            const auto shared_backlog_root = root / "_kano" / "backlog";
            const auto shared_product_root = shared_backlog_root / "products" / "kano-agent-ark-skill";
            std::filesystem::create_directories(shared_product_root / "items");
            std::filesystem::create_directories(shared_product_root / "views");
            std::filesystem::create_directories(shared_product_root / "_meta");

            BacklogIndex shared_index(shared_backlog_root / ".cache" / "index" / "backlog.db");
            shared_index.initialize();
            CanonicalStore shared_store(shared_product_root);

            auto shared_parent_created = WorkitemOps::create_item(
                shared_index,
                shared_product_root,
                "KOA",
                ItemType::Feature,
                "KOA shared parent sync smoke",
                "opencode");
            auto shared_parent = shared_store.read(shared_parent_created.path);
            set_ready_fields(shared_parent);
            shared_store.write(shared_parent);
            shared_index.index_item(shared_parent);

            auto shared_child_created = WorkitemOps::create_item(
                shared_index,
                shared_product_root,
                "KOA",
                ItemType::Task,
                "KOA shared child state smoke",
                "opencode",
                shared_parent_created.id);
            auto shared_child = shared_store.read(shared_child_created.path);
            set_ready_fields(shared_child);
            shared_store.write(shared_child);
            shared_index.index_item(shared_child);

            auto host_only_parent = shared_parent;
            const auto host_only_parent_path = std::filesystem::temp_directory_path() /
                "koa-host-only-backlog" /
                "products" /
                "kano-agent-ark-skill" /
                "items" /
                "feature" /
                "0000" /
                (shared_parent_created.id + "_host-only.md");
            host_only_parent.file_path = host_only_parent_path;
            shared_index.index_item(host_only_parent);

            auto shared_update = WorkitemOps::update_state(
                shared_index,
                shared_product_root,
                shared_child_created.id,
                ItemState::InProgress,
                "opencode");
            expect(shared_update.parent_synced, "shared-layout parent should sync despite stale host-only index path");

            auto shared_parent_synced = shared_store.read(shared_parent_created.path);
            expect(
                shared_parent_synced.state == ItemState::InProgress,
                "shared-layout real parent should be synced under the active product root");

            external_root = root.parent_path() / (root.filename().string() + "-external");
            std::filesystem::create_directories(external_root / "items");
            std::filesystem::create_directories(external_root / "views");
            std::filesystem::create_directories(external_root / "_meta");
            CanonicalStore external_store(external_root);
            BacklogIndex external_index(external_root / ".cache" / "index" / "backlog.db");
            external_index.initialize();
            auto external_created = WorkitemOps::create_item(
                external_index,
                external_root,
                "EXT",
                ItemType::Feature,
                "External parent must stay isolated",
                "opencode");
            auto external_parent = external_store.read(external_created.path);
            set_ready_fields(external_parent);
            external_store.write(external_parent);
            external_index.index_item(external_parent);

            auto outside_index_parent_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Feature,
                "Existing outside indexed path parent",
                "opencode");
            auto outside_index_parent = store.read(outside_index_parent_created.path);
            set_ready_fields(outside_index_parent);
            store.write(outside_index_parent);
            index.index_item(outside_index_parent);

            auto stale_existing_external_path = outside_index_parent;
            stale_existing_external_path.file_path = external_created.path;
            index.index_item(stale_existing_external_path);

            auto outside_index_child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Existing outside indexed path child",
                "opencode",
                outside_index_parent_created.id);
            auto outside_index_child = store.read(outside_index_child_created.path);
            set_ready_fields(outside_index_child);
            store.write(outside_index_child);
            index.index_item(outside_index_child);

            auto outside_index_update = WorkitemOps::update_state(
                index,
                root,
                outside_index_child_created.id,
                ItemState::InProgress,
                "opencode");
            expect(
                outside_index_update.parent_synced,
                "parent should sync when stale indexed path points to an existing outside-root file");

            auto outside_index_parent_synced = store.read(outside_index_parent_created.path);
            expect(
                outside_index_parent_synced.state == ItemState::InProgress,
                "active parent should sync despite an existing outside-root indexed path");

            auto external_parent_after_stale_index = external_store.read(external_created.path);
            expect(
                external_parent_after_stale_index.state == ItemState::Proposed,
                "existing outside-root indexed file should not be read or mutated");

            auto path_parent_child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Path parent must be rejected",
                "opencode",
                external_created.path.string());
            auto path_parent_child = store.read(path_parent_child_created.path);
            set_ready_fields(path_parent_child);
            store.write(path_parent_child);
            index.index_item(path_parent_child);

            std::string path_parent_diagnostic;
            bool rejected_external_parent_path = false;
            try {
                (void)WorkitemOps::update_state(
                    index,
                    root,
                    path_parent_child_created.id,
                    ItemState::InProgress,
                    "opencode");
            } catch (const std::exception& ex) {
                rejected_external_parent_path = true;
                path_parent_diagnostic = ex.what();
            }
            expect(rejected_external_parent_path, "path-like parent refs outside product root should be rejected");
            expect(
                path_parent_diagnostic.find("Parent item not found") != std::string::npos,
                "path-like parent rejection should emit an explicit missing-parent diagnostic");
            expect(
                path_parent_diagnostic.find("path-like parent ref redacted") != std::string::npos,
                "path-like parent rejection should redact the raw parent path");
            expect(
                path_parent_diagnostic.find(external_created.path.string()) == std::string::npos,
                "path-like parent rejection should not echo the external parent path");

            auto unchanged_external_parent = external_store.read(external_created.path);
            expect(unchanged_external_parent.state == ItemState::Proposed, "external parent should not be mutated");

            auto rejected_child = store.read(path_parent_child_created.path);
            expect(rejected_child.state == ItemState::Proposed, "child with rejected path parent should not be updated");

            auto stale_missing_parent_ref = std::string("TST-FTR-9998");
            auto stale_missing_parent_index = outside_index_parent;
            stale_missing_parent_index.id = stale_missing_parent_ref;
            stale_missing_parent_index.uid = stale_missing_parent_ref + "-uid";
            stale_missing_parent_index.file_path = external_created.path;
            index.index_item(stale_missing_parent_index);

            auto stale_missing_child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Stale indexed missing parent child smoke",
                "opencode",
                stale_missing_parent_ref);
            auto stale_missing_child = store.read(stale_missing_child_created.path);
            set_ready_fields(stale_missing_child);
            store.write(stale_missing_child);
            index.index_item(stale_missing_child);

            std::string stale_missing_parent_diagnostic;
            bool rejected_stale_missing_parent = false;
            try {
                (void)WorkitemOps::update_state(
                    index,
                    root,
                    stale_missing_child_created.id,
                    ItemState::InProgress,
                    "opencode");
            } catch (const std::exception& ex) {
                rejected_stale_missing_parent = true;
                stale_missing_parent_diagnostic = ex.what();
            }
            expect(rejected_stale_missing_parent, "stale indexed missing parent should be rejected");
            expect(
                stale_missing_parent_diagnostic.find("stale index/path cache") != std::string::npos,
                "stale indexed missing parent diagnostic should mention stale index/path cache");
            expect(
                stale_missing_parent_diagnostic.find("outside-active-root") != std::string::npos,
                "stale indexed missing parent diagnostic should classify outside-root index paths");
            expect(
                stale_missing_parent_diagnostic.find(external_created.path.string()) == std::string::npos,
                "stale indexed missing parent diagnostic should not echo outside-root paths");

            // 3. Renamed/moved parent slug under active root: the index still points at
            //    the original slug path, but the file is reachable under a new slug. The
            //    identity-scan fallback must resolve the parent without trusting the
            //    stale indexed path.
            auto renamed_parent_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Feature,
                "Original renamed slug parent",
                "opencode");
            auto renamed_parent = store.read(renamed_parent_created.path);
            set_ready_fields(renamed_parent);
            store.write(renamed_parent);
            index.index_item(renamed_parent);

            std::filesystem::path renamed_parent_path = renamed_parent_created.path;
            std::filesystem::path renamed_parent_target = renamed_parent_path.parent_path() /
                (renamed_parent_created.id + "_renamed-slug-parent.md");
            std::filesystem::rename(renamed_parent_path, renamed_parent_target);

            // Re-index the renamed parent under its new path so the active store is
            // self-consistent even though the lookup path goes through the stale row.
            auto renamed_parent_after = store.read(renamed_parent_target);
            index.index_item(renamed_parent_after);

            // Plant the stale indexed row pointing at the original (now-gone) slug
            // path under the active product root. This mirrors the production
            // scenario where the cache still references the pre-rename file.
            auto stale_renamed_item = renamed_parent_after;
            stale_renamed_item.file_path = renamed_parent_path;
            index.index_item(stale_renamed_item);

            auto renamed_child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Renamed slug child state smoke",
                "opencode",
                renamed_parent_created.id);
            auto renamed_child = store.read(renamed_child_created.path);
            set_ready_fields(renamed_child);
            store.write(renamed_child);
            index.index_item(renamed_child);

            auto renamed_update = WorkitemOps::update_state(
                index,
                root,
                renamed_child_created.id,
                ItemState::InProgress,
                "opencode");
            expect(renamed_update.parent_synced, "parent should sync after slug rename via identity scan");

            auto renamed_parent_synced = store.read(renamed_parent_target);
            expect(
                renamed_parent_synced.state == ItemState::InProgress,
                "renamed-slug parent should be synced to InProgress at its new path");

            // 4. Truly missing parent: no indexed path, no identity match. The
            //    diagnostic must explicitly identify the parent as missing.
            auto missing_parent_ref = std::string("TST-FTR-9999");
            auto missing_parent_child_created = WorkitemOps::create_item(
                index,
                root,
                "TST",
                ItemType::Task,
                "Missing parent child smoke",
                "opencode",
                missing_parent_ref);
            auto missing_parent_child = store.read(missing_parent_child_created.path);
            set_ready_fields(missing_parent_child);
            store.write(missing_parent_child);
            index.index_item(missing_parent_child);

            std::string missing_parent_diagnostic;
            bool rejected_missing_parent = false;
            try {
                (void)WorkitemOps::update_state(
                    index,
                    root,
                    missing_parent_child_created.id,
                    ItemState::InProgress,
                    "opencode");
            } catch (const std::exception& ex) {
                rejected_missing_parent = true;
                missing_parent_diagnostic = ex.what();
            }
            expect(rejected_missing_parent, "truly missing parent should be rejected");
            expect(
                missing_parent_diagnostic.find("Parent item not found in active product root") != std::string::npos,
                "missing-parent diagnostic should explicitly identify the missing active parent");
            expect(
                missing_parent_diagnostic.find(missing_parent_ref) != std::string::npos,
                "missing-parent diagnostic should reference the unresolved parent ref");

            auto unchanged_missing_parent_child = store.read(missing_parent_child_created.path);
            expect(
                unchanged_missing_parent_child.state == ItemState::Proposed,
                "child with truly missing parent should not be updated");
        }

        std::filesystem::remove_all(external_root);
        std::cout << "workitem_ops_smoke_test: PASS\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& ex) {
        if (!root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(root, cleanup_error);
        }
        if (!external_root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(external_root, cleanup_error);
        }
        std::cerr << "workitem_ops_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
