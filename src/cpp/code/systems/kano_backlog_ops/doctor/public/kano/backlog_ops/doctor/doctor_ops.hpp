#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace kano::backlog_ops {

struct DoctorCheckResult {
    std::string name;
    bool passed;
    std::string message;
    std::string details;
};

struct DoctorOptions {
    std::filesystem::path start_path = ".";
    std::optional<std::filesystem::path> backlog_root;
    std::optional<std::filesystem::path> config_path;
};

class DoctorOps {
public:
    /**
     * Run all environment health checks.
     * Native doctor command result.
     */
    static std::vector<DoctorCheckResult> run_all_checks(const DoctorOptions& options);
    static std::vector<DoctorCheckResult> run_all_checks(const std::filesystem::path& start_path);

private:
    static DoctorCheckResult check_backlog_structure(const std::filesystem::path& root);
    static DoctorCheckResult check_backlog_initialized(
        const std::filesystem::path& root,
        const std::optional<std::filesystem::path>& config_path
    );
    static DoctorCheckResult check_sqlite_status(const std::filesystem::path& root);
};

} // namespace kano::backlog_ops
