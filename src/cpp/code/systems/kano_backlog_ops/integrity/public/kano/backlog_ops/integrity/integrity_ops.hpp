#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kano::backlog_ops {

struct IntegrityOptions {
    std::filesystem::path backlog_root;
    std::vector<std::string> products;
    std::string as_of;
    int stale_days = 90;
};

struct IntegrityFinding {
    std::string issue_type;
    std::string rule_id;
    std::string severity;
    std::string product;
    std::string item_id;
    std::filesystem::path path;
    std::string message;
};

struct IntegrityReport {
    std::filesystem::path backlog_root;
    std::string as_of;
    int stale_days = 90;
    std::vector<std::string> products_scanned;
    int items_scanned = 0;
    std::vector<IntegrityFinding> findings;
};

class IntegrityOps {
public:
    static IntegrityReport inspect(const IntegrityOptions& options);
    static std::string render_markdown(const IntegrityReport& report);
    static std::string render_json(const IntegrityReport& report);
};

} // namespace kano::backlog_ops
