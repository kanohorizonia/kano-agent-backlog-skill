#include <filesystem>
#include <iostream>
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

void set_ready_fields(kano::backlog_core::BacklogItem& item) {
    item.context = "Need deterministic parent sync coverage.";
    item.goal = "Update a child item without trusting stale indexed host paths.";
    item.approach = "Use isolated temp roots and a deliberately stale parent index row.";
    item.acceptance_criteria = "Child state update syncs the real parent file.";
    item.risks = "Low risk - isolated smoke fixture.";
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
