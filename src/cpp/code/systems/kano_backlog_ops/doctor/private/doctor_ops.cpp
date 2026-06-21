#include "kano/backlog_ops/doctor/doctor_ops.hpp"
#include "kano/backlog_core/config/config.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <sqlite3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kano::backlog_ops {

namespace {

using kano::backlog_core::ConfigLoader;
using kano::backlog_core::ProjectConfig;

std::filesystem::path normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
        ec.clear();
    }
    auto normalized = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) {
        normalized = absolute.lexically_normal();
    }
    return normalized;
}

bool exists_path(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::filesystem::path start_directory(const std::filesystem::path& start_path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(start_path.empty() ? std::filesystem::path(".") : start_path, ec);
    if (ec) {
        absolute = start_path.empty() ? std::filesystem::path(".") : start_path;
        ec.clear();
    }
    if (std::filesystem::is_directory(absolute, ec)) {
        return absolute.lexically_normal();
    }
    auto parent = absolute.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    return normalized_absolute_path(parent);
}

bool looks_like_backlog_root(const std::filesystem::path& path) {
    return exists_path(path / "products") || exists_path(path / "items");
}

std::string join_strings(const std::vector<std::string>& values, const std::string& separator = ", ") {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << separator;
        }
        out << values[i];
    }
    return out.str();
}

std::vector<std::string> sorted_product_names(const ProjectConfig& config) {
    std::vector<std::string> products;
    for (const auto& [name, _] : config.products) {
        products.push_back(name);
    }
    std::sort(products.begin(), products.end());
    return products;
}

std::optional<std::filesystem::path> first_existing_config(const std::vector<std::filesystem::path>& candidates) {
    for (const auto& candidate : candidates) {
        if (exists_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> doctor_config_candidates(
    const std::filesystem::path& start_path,
    const std::optional<std::filesystem::path>& backlog_root
) {
    std::vector<std::filesystem::path> candidates;
    std::set<std::string> seen;
    auto add_candidate = [&](const std::filesystem::path& candidate) {
        std::error_code ec;
        auto display_path = std::filesystem::absolute(candidate, ec);
        if (ec) {
            display_path = candidate;
            ec.clear();
        }
        display_path = display_path.lexically_normal();
        const auto key = normalized_absolute_path(display_path).string();
        if (seen.insert(key).second) {
            candidates.push_back(display_path);
        }
    };

    auto current = start_directory(start_path);
    while (true) {
        add_candidate(current / ".kano" / "backlog_config.toml");
        add_candidate(current / "_kano" / "backlog" / ".kano" / "backlog_config.toml");
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }

    if (backlog_root && !backlog_root->empty()) {
        const auto root = normalized_absolute_path(*backlog_root);
        add_candidate(root / ".kano" / "backlog_config.toml");
        if (root.filename() == "backlog" && root.parent_path().filename() == "_kano") {
            add_candidate(root.parent_path().parent_path() / ".kano" / "backlog_config.toml");
        }
    }

    return candidates;
}

std::optional<std::filesystem::path> resolve_backlog_root_from_config(
    const ProjectConfig& config,
    const std::filesystem::path& config_path
) {
    for (const auto& product_name : sorted_product_names(config)) {
        auto product_root = config.resolve_backlog_root(product_name, config_path);
        if (!product_root) {
            continue;
        }
        const auto normalized_product_root = normalized_absolute_path(*product_root);
        if (normalized_product_root.parent_path().filename() == "products") {
            return normalized_product_root.parent_path().parent_path();
        }
        if (looks_like_backlog_root(normalized_product_root)) {
            return normalized_product_root;
        }
        return normalized_product_root;
    }

    auto project_root = ConfigLoader::resolve_project_root(config_path);
    if (project_root) {
        const auto shared_root = *project_root / "_kano" / "backlog";
        if (looks_like_backlog_root(shared_root)) {
            return normalized_absolute_path(shared_root);
        }
        if (looks_like_backlog_root(*project_root)) {
            return normalized_absolute_path(*project_root);
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_backlog_root_from_config_location(
    const std::filesystem::path& config_path
) {
    auto project_root = ConfigLoader::resolve_project_root(config_path);
    if (!project_root) {
        return std::nullopt;
    }

    if (looks_like_backlog_root(*project_root)) {
        return normalized_absolute_path(*project_root);
    }

    const auto shared_root = *project_root / "_kano" / "backlog";
    if (looks_like_backlog_root(shared_root)) {
        return normalized_absolute_path(shared_root);
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> find_backlog_root_from_layout(const std::filesystem::path& start_path) {
    auto current = start_directory(start_path);
    while (true) {
        if (looks_like_backlog_root(current)) {
            return normalized_absolute_path(current);
        }
        if (std::filesystem::exists(current / "_kano" / "backlog")) {
            return normalized_absolute_path(current / "_kano" / "backlog");
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::nullopt;
}

std::string checked_config_details(const std::vector<std::filesystem::path>& candidates) {
    if (candidates.empty()) {
        return "Checked config paths: none";
    }
    std::ostringstream out;
    out << "Checked config paths:";
    for (const auto& candidate : candidates) {
        out << "\n       - " << candidate.string();
    }
    return out.str();
}

std::string recommended_command(const std::filesystem::path& backlog_root) {
    if (backlog_root.empty()) {
        return "Run kano-backlog admin init, or pass --path/--config to an existing backlog config.";
    }
    std::ostringstream out;
    out << "cd " << backlog_root.string()
        << " or run kano-backlog --path " << backlog_root.string() << " doctor";
    return out.str();
}

#ifdef _WIN32
std::optional<std::filesystem::path> windows_temp_directory_path_alias(const std::filesystem::path& path) {
    std::error_code ec;
    const auto temp_root = std::filesystem::temp_directory_path(ec);
    if (ec || temp_root.empty()) {
        return std::nullopt;
    }

    const auto normalized_temp = normalized_absolute_path(temp_root);
    const auto normalized_path = normalized_absolute_path(path);
    auto relative = std::filesystem::relative(normalized_path, normalized_temp, ec);
    if (ec || relative.empty() || relative.is_absolute()) {
        return std::nullopt;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    return (temp_root / relative).lexically_normal();
}

std::optional<std::filesystem::path> windows_short_path(const std::filesystem::path& path) {
    const auto wide_path = path.wstring();
    const DWORD required = GetShortPathNameW(wide_path.c_str(), nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring buffer(required, L'\0');
    const DWORD written = GetShortPathNameW(wide_path.c_str(), buffer.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    buffer.resize(written);
    return std::filesystem::path(buffer);
}

void append_windows_short_path_alias(
    std::ostringstream& details,
    const std::string& label,
    const std::filesystem::path& path
) {
    const auto short_path = windows_short_path(path);
    if (short_path && short_path->string() != path.string()) {
        details << "\n       " << label << " short path: " << short_path->string();
    }
    const auto temp_alias = windows_temp_directory_path_alias(path);
    if (temp_alias && temp_alias->string() != path.string() && (!short_path || temp_alias->string() != short_path->string())) {
        details << "\n       " << label << " temp path: " << temp_alias->string();
    }
}
#else
void append_windows_short_path_alias(
    std::ostringstream&,
    const std::string&,
    const std::filesystem::path&
) {}
#endif

struct DoctorDiscovery {
    std::filesystem::path start_path;
    std::vector<std::filesystem::path> checked_config_paths;
    std::optional<std::filesystem::path> backlog_root;
    std::optional<std::filesystem::path> config_path;
    std::optional<ProjectConfig> project_config;
    std::vector<std::string> products;
    std::string config_error;
    bool explicit_backlog_root = false;
    bool explicit_config = false;
    bool shared_layout = false;
};

DoctorDiscovery discover_backlog(const DoctorOptions& options) {
    DoctorDiscovery discovery;
    discovery.start_path = start_directory(
        options.start_path.empty() ? std::filesystem::path(".") : options.start_path
    );

    if (options.backlog_root && !options.backlog_root->empty()) {
        discovery.explicit_backlog_root = true;
        discovery.backlog_root = normalized_absolute_path(*options.backlog_root);
    }

    if (options.config_path && !options.config_path->empty()) {
        discovery.explicit_config = true;
        discovery.config_path = normalized_absolute_path(*options.config_path);
        discovery.checked_config_paths.push_back(*discovery.config_path);
    } else {
        discovery.checked_config_paths = doctor_config_candidates(discovery.start_path, discovery.backlog_root);
        discovery.config_path = first_existing_config(discovery.checked_config_paths);
        if (!discovery.config_path && discovery.backlog_root) {
            const auto root_config = *discovery.backlog_root / ".kano" / "backlog_config.toml";
            discovery.checked_config_paths.push_back(root_config);
            if (exists_path(root_config)) {
                discovery.config_path = root_config;
            }
        }
    }

    if (discovery.config_path && exists_path(*discovery.config_path)) {
        try {
            discovery.project_config = ProjectConfig::load_from_toml(*discovery.config_path);
            if (discovery.project_config) {
                discovery.products = sorted_product_names(*discovery.project_config);
                if (!discovery.backlog_root) {
                    discovery.backlog_root = resolve_backlog_root_from_config_location(*discovery.config_path);
                }
                if (!discovery.backlog_root) {
                    discovery.backlog_root = resolve_backlog_root_from_config(*discovery.project_config, *discovery.config_path);
                }
            }
        } catch (const std::exception& ex) {
            discovery.config_error = ex.what();
        }
    }

    if (!discovery.backlog_root) {
        discovery.backlog_root = find_backlog_root_from_layout(discovery.start_path);
    }

    if (discovery.backlog_root) {
        const auto root = discovery.backlog_root->lexically_normal();
        discovery.shared_layout = root.filename() == "backlog" && root.parent_path().filename() == "_kano";
    }

    return discovery;
}

DoctorCheckResult check_backlog_discovery(const DoctorDiscovery& discovery) {
    DoctorCheckResult res;
    res.name = "Backlog Discovery";

    if (!discovery.config_error.empty()) {
        res.passed = false;
        res.message = "Project config could not be parsed";
        std::ostringstream details;
        details << "Config path: " << (discovery.config_path ? discovery.config_path->string() : std::string("<none>"))
                << "\n       Error: " << discovery.config_error;
        res.details = details.str();
        return res;
    }

    if (!discovery.backlog_root) {
        res.passed = false;
        res.message = "Backlog root not found from " + discovery.start_path.string();
        std::ostringstream details;
        details << checked_config_details(discovery.checked_config_paths)
                << "\n       Shared-layout candidate: "
                << (start_directory(discovery.start_path) / "_kano" / "backlog" / ".kano" / "backlog_config.toml").lexically_normal().string()
                << "\n       Recommended: " << recommended_command({});
        res.details = details.str();
        return res;
    }

    res.passed = true;
    res.message = discovery.shared_layout ? "Detected shared backlog root" : "Detected backlog root";
    std::ostringstream details;
    details << "Backlog root: " << discovery.backlog_root->string();
    append_windows_short_path_alias(details, "Backlog root", *discovery.backlog_root);
    if (discovery.config_path && exists_path(*discovery.config_path)) {
        details << "\n       Config path: " << discovery.config_path->string();
        append_windows_short_path_alias(details, "Config path", *discovery.config_path);
    } else {
        details << "\n       Config path: not found";
    }
    details << "\n       Available products: "
            << (discovery.products.empty() ? std::string("<none>") : join_strings(discovery.products));
    if (discovery.explicit_backlog_root || discovery.explicit_config) {
        details << "\n       Override: "
                << (discovery.explicit_backlog_root ? "--backlog-root " : "")
                << (discovery.explicit_config ? "--config" : "");
    }
    details << "\n       Recommended: " << recommended_command(*discovery.backlog_root);
    res.details = details.str();
    return res;
}

DoctorCheckResult check_product_prefix_uniqueness(const DoctorDiscovery& discovery) {
    DoctorCheckResult res;
    res.name = "Product Prefix Uniqueness";

    if (!discovery.project_config || !discovery.config_path || !exists_path(*discovery.config_path)) {
        res.passed = true;
        res.message = "No parsed product config available";
        return res;
    }

    const auto collisions = discovery.project_config->find_prefix_collisions(*discovery.config_path);
    if (collisions.empty()) {
        res.passed = true;
        res.message = "No duplicate product prefixes found";
        return res;
    }

    res.passed = false;
    res.message = "Duplicate product prefixes found";
    res.details = ProjectConfig::describe_prefix_collisions(collisions);
    return res;
}

} // namespace

std::vector<DoctorCheckResult> DoctorOps::run_all_checks(const DoctorOptions& options) {
    std::vector<DoctorCheckResult> results;
    const auto discovery = discover_backlog(options);
    const auto backlog_root = discovery.backlog_root.value_or(std::filesystem::path{});

    results.push_back(check_backlog_discovery(discovery));
    results.push_back(check_product_prefix_uniqueness(discovery));
    results.push_back(check_backlog_structure(backlog_root));
    results.push_back(check_backlog_initialized(backlog_root, discovery.config_path));
    results.push_back(check_sqlite_status(backlog_root));

    return results;
}

std::vector<DoctorCheckResult> DoctorOps::run_all_checks(const std::filesystem::path& start_path) {
    DoctorOptions options;
    options.start_path = start_path;
    return run_all_checks(options);
}

DoctorCheckResult DoctorOps::check_backlog_structure(const std::filesystem::path& root) {
    DoctorCheckResult res;
    res.name = "Backlog Structure";
    
    if (root.empty() || !std::filesystem::exists(root)) {
        res.passed = false;
        res.message = "Backlog root not found";
        res.details = "Run 'kano-backlog admin init' to initialize.";
        return res;
    }

    std::vector<std::string> missing;
    if (!std::filesystem::exists(root / "products")) missing.push_back("products");
    
    if (!missing.empty()) {
        res.passed = false;
        res.message = "Missing required directories";
        std::stringstream ss;
        ss << "Missing: ";
        for (const auto& m : missing) ss << m << " ";
        res.details = ss.str();
    } else {
        res.passed = true;
        res.message = "Backlog structure is valid";
    }
    return res;
}

DoctorCheckResult DoctorOps::check_backlog_initialized(
    const std::filesystem::path& root,
    const std::optional<std::filesystem::path>& config_path
) {
    DoctorCheckResult res;
    res.name = "Backlog Initialized";
    
    if (root.empty() || !std::filesystem::exists(root)) {
        res.passed = false;
        res.message = "Cannot check initialization without root";
        return res;
    }

    const auto expected_config_path = config_path && !config_path->empty()
        ? *config_path
        : root / ".kano" / "backlog_config.toml";
    
    if (!std::filesystem::exists(expected_config_path)) {
        res.passed = false;
        res.message = "Project config not found";
        res.details = "Expected: " + expected_config_path.string()
            + "\n       Recommended: " + recommended_command(root);
    } else {
        res.passed = true;
        res.message = "Project config found at " + expected_config_path.string();
    }
    return res;
}

DoctorCheckResult DoctorOps::check_sqlite_status(const std::filesystem::path& root) {
    DoctorCheckResult res;
    res.name = "SQLite Status";
    
    res.message = "SQLite version: " + std::string(sqlite3_libversion());
    
    if (!root.empty()) {
        auto db_path = root / ".cache" / "index" / "backlog.db";
        if (std::filesystem::exists(db_path)) {
            sqlite3* db;
            if (sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK) {
                res.passed = true;
                res.details = "Database index found and accessible: " + db_path.string();
                sqlite3_close(db);
            } else {
                res.passed = false;
                res.details = "Database index found but NOT accessible: " + db_path.string();
            }
        } else {
            res.passed = true; // Informational
            res.message += " (No index DB found yet)";
            res.details = "Expected: " + db_path.string();
        }
    } else {
        res.passed = true;
    }
    
    return res;
}

} // namespace kano::backlog_ops
