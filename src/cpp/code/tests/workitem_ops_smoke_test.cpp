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

            bool rejected_external_parent_path = false;
            try {
                (void)WorkitemOps::update_state(
                    index,
                    root,
                    path_parent_child_created.id,
                    ItemState::InProgress,
                    "opencode");
            } catch (const std::exception&) {
                rejected_external_parent_path = true;
            }
            expect(rejected_external_parent_path, "path-like parent refs outside product root should be rejected");

            auto unchanged_external_parent = external_store.read(external_created.path);
            expect(unchanged_external_parent.state == ItemState::Proposed, "external parent should not be mutated");

            auto rejected_child = store.read(path_parent_child_created.path);
            expect(rejected_child.state == ItemState::Proposed, "child with rejected path parent should not be updated");
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
