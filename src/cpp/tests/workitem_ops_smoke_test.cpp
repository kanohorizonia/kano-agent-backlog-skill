#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

#include "backlog_adapters/fs/filesystem.hpp"
#include "backlog_core/model/backlog_item.hpp"
#include "backlog_ops/workitem/workitem_ops.hpp"

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
    using kano::backlog::adapters::LocalFilesystem;
    using kano::backlog::core::ItemState;
    using kano::backlog::core::ItemType;
    using kano::backlog::ops::WorkitemOps;

    std::filesystem::path root;

    try {
        root = make_temp_root();
        auto fs = std::make_shared<LocalFilesystem>();
        WorkitemOps workitems(fs, root);

        auto created = workitems.create_item(ItemType::Task, "Native workitem smoke", "guide-test", "opencode");
        expect(created.state == ItemState::Proposed, "new item should start Proposed");

        auto loaded = workitems.get_item(created.id);
        expect(loaded.has_value(), "created item should be readable");
        expect(loaded->title == "Native workitem smoke", "loaded title mismatch");

        const bool ready_fields_ok = workitems.set_ready_fields(
            created.id,
            std::string("Need deterministic native workitem coverage."),
            std::string("Exercise create, ready, update, and worklog flows."),
            std::string("Use isolated temp directories in smoke tests."),
            std::string("Smoke test passes through native code paths."),
            std::string("Low."),
            "opencode");
        expect(ready_fields_ok, "set_ready_fields should succeed");

        expect(workitems.update_state(created.id, ItemState::Ready), "Ready transition should succeed");
        expect(workitems.update_state(created.id, ItemState::InProgress), "InProgress transition should succeed");
        expect(workitems.append_worklog(created.id, "Ran native smoke validation", "opencode"),
            "append_worklog should succeed");

        auto updated = workitems.get_item(created.id);
        expect(updated.has_value(), "updated item should be readable");
        expect(updated->state == ItemState::InProgress, "updated state mismatch");
        expect(updated->context.has_value(), "context should be populated after set_ready_fields");
        expect(updated->worklog.size() >= 2, "worklog should include appended entry");

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
