#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

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

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    using kano::backlog_core::ItemState;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;

    std::filesystem::path root;

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
        }

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
