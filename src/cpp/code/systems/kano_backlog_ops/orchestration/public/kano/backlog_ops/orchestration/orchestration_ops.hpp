#pragma once

#include "kano/backlog_ops/index/backlog_index.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kano::backlog_ops {

/**
 * OrchestrationOps manages high-level backlog workflows.
 * Ported from init.py and other coordination logic.
 */
class OrchestrationOps {
public:
    struct InitOptions {
        std::filesystem::path start_path = ".";
        std::optional<std::filesystem::path> backlog_root;
        std::string product;
        std::string agent;
        std::optional<std::string> product_name;
        std::optional<std::string> prefix;
        bool force = false;
        bool refresh_views = true;
    };

    struct InitResult {
        std::filesystem::path project_root;
        std::filesystem::path backlog_root;
        std::filesystem::path product_root;
        std::filesystem::path config_path;
        std::vector<std::filesystem::path> created_paths;
        std::vector<std::filesystem::path> views_refreshed;
    };

    /**
     * Initialize a product backlog and register it in the project config.
     */
    static InitResult initialize_backlog(const InitOptions& options);

    /**
     * Refresh the index by scanning all item files.
     */
    static void refresh_index(BacklogIndex& index, const std::filesystem::path& root);
};

} // namespace kano::backlog_ops
