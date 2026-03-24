// Smoke test for workitem_ops using new layer (code/systems/kano_backlog_ops)
// Updated 2026-03-24 to properly test the Ready gate flow

#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
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
    return root;
}

} // namespace

int main() {
    using kano::backlog_core::BacklogItem;
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;

    std::filesystem::path root;
    std::filesystem::path db_path;

    try {
        root = make_temp_root();
        db_path = root / ".cache" / "index.db";
        std::filesystem::create_directories(db_path.parent_path());

        BacklogIndex index(db_path);
        index.initialize();

        // Create item - starts in Proposed state
        auto created = WorkitemOps::create_item(
            index, root, "GT", ItemType::Task, "Native workitem smoke", "opencode");
        expect(!created.id.empty(), "create_item should return non-empty id");
        expect(std::filesystem::exists(created.path), "created item file should exist");

        // Verify initial state is Proposed
        CanonicalStore store(root);
        BacklogItem item = store.read(created.path);
        expect(item.state == ItemState::Proposed, "new item should start in Proposed state");

        // Set required Ready gate fields
        item.context = "Need deterministic native workitem coverage.";
        item.goal = "Exercise create, ready, update, and worklog flows.";
        item.approach = "Use isolated temp directories in smoke tests.";
        item.acceptance_criteria = "Smoke test passes through native code paths.";
        item.risks = "Low risk - isolated temp directories.";

        // Write back with updated fields
        store.write(item);
        index.index_item(item);

        // Transition to Ready (requires all fields)
        auto ready_result = WorkitemOps::update_state(
            index, root, created.id, ItemState::Ready, "opencode");
        expect(ready_result.new_state == ItemState::Ready, "state should be Ready");

        // Transition to InProgress
        auto progress_result = WorkitemOps::update_state(
            index, root, created.id, ItemState::InProgress, "opencode");
        expect(progress_result.new_state == ItemState::InProgress, "state should be InProgress");

        // Verify final state by reading back
        BacklogItem final_item = store.read(created.path);
        expect(final_item.state == ItemState::InProgress, "final state should be InProgress");
        expect(final_item.context.has_value(), "context should be set");
        expect(final_item.worklog.size() >= 3, "worklog should have at least 3 entries (created + ready + inprogress)");

        std::cout << "workitem_ops_smoke_test: PASS\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& ex) {
        if (!root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(root, cleanup_error);
        }
        std::cerr << "workitem_ops_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
