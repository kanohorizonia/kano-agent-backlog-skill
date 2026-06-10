#include "kano/backlog_ops/orchestration/orchestration_ops.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_ops/view/view_ops.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kItemTypes[] = {"epic", "feature", "userstory", "task", "bug"};

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string normalize_product_name(const std::string& product) {
    std::string cleaned = trim(product);
    if (cleaned.empty()) {
        throw std::runtime_error("Product ID is required");
    }
    const auto is_safe = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_';
    };
    if (!std::isalnum(static_cast<unsigned char>(cleaned.front())) ||
        !std::isalnum(static_cast<unsigned char>(cleaned.back())) ||
        !std::all_of(cleaned.begin(), cleaned.end(), [&](unsigned char ch) { return is_safe(ch); })) {
        throw std::runtime_error("Product ID must use only letters, numbers, hyphens, or underscores, and must start and end with a letter or number");
    }
    return cleaned;
}

std::string normalize_agent_id(const std::string& agent) {
    std::string cleaned = trim(agent);
    if (cleaned.empty()) {
        throw std::runtime_error("Agent ID is required");
    }
    const auto is_safe = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
    };
    if (!std::all_of(cleaned.begin(), cleaned.end(), [&](unsigned char ch) { return is_safe(ch); })) {
        throw std::runtime_error("Agent ID must use only letters, numbers, dots, hyphens, or underscores");
    }
    return cleaned;
}

std::string derive_prefix(std::string product_name) {
    product_name = trim(product_name);
    std::transform(product_name.begin(), product_name.end(), product_name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    std::vector<std::string> segments;
    std::string current;
    for (char ch : product_name) {
        if (ch == '-' || ch == '_' || std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        segments.push_back(current);
    }

    std::string prefix;
    if (segments.size() >= 2) {
        prefix.push_back(segments[0][0]);
        prefix.push_back(segments[1][0]);
    } else if (segments.size() == 1 && !segments[0].empty()) {
        const std::string& seed = segments[0];
        prefix.push_back(seed[0]);
        for (std::size_t index = 1; index < seed.size(); ++index) {
            const char ch = seed[index];
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (std::isalpha(static_cast<unsigned char>(ch)) && std::string("AEIOU").find(upper) == std::string::npos) {
                prefix.push_back(ch);
                break;
            }
        }
        if (prefix.size() < 2) {
            for (std::size_t index = 1; index < seed.size(); ++index) {
                const char ch = seed[index];
                if (std::isalpha(static_cast<unsigned char>(ch))) {
                    prefix.push_back(ch);
                    break;
                }
            }
        }
    }

    if (prefix.size() < 2) {
        prefix = "XX";
    }

    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return prefix;
}

bool ensure_dir(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        return false;
    }
    std::filesystem::create_directories(path);
    return true;
}

std::filesystem::path find_repo_root(const std::filesystem::path& start_path) {
    std::filesystem::path current = std::filesystem::is_directory(start_path) ? start_path : start_path.parent_path();
    while (true) {
        if (std::filesystem::exists(current / ".git")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return start_path;
}

bool is_within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const std::filesystem::path rel = child.lexically_relative(parent);
    return !rel.empty() && rel.generic_string().find("..") != 0 && !rel.is_absolute();
}

std::filesystem::path resolve_backlog_root(const kano::backlog_ops::OrchestrationOps::InitOptions& options) {
    const std::filesystem::path start = std::filesystem::absolute(options.start_path).lexically_normal();
    const std::filesystem::path project_root = find_repo_root(start).lexically_normal();
    if (options.backlog_root) {
        std::filesystem::path root = options.backlog_root->is_absolute()
            ? *options.backlog_root
            : project_root / *options.backlog_root;
        root = root.lexically_normal();
        if (root.filename() == "products") {
            root = root.parent_path();
        }
        if (!is_within(root, project_root) && root != project_root) {
            throw std::runtime_error("Backlog root must stay inside the project root");
        }
        return root;
    }

    return project_root / "_kano" / "backlog";
}

std::filesystem::path resolve_project_root(const std::filesystem::path& backlog_root) {
    if (backlog_root.parent_path().filename() == "_kano") {
        return backlog_root.parent_path().parent_path();
    }
    return backlog_root.parent_path();
}

std::string to_posix(const std::filesystem::path& path) {
    std::string value = path.generic_string();
    return value;
}

std::string relativize(const std::filesystem::path& path, const std::filesystem::path& base) {
    try {
        return to_posix(std::filesystem::relative(path, base));
    } catch (const std::exception&) {
        return to_posix(path);
    }
}

std::string toml_string(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    out << '"';
    return out.str();
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw_time);
#else
    gmtime_r(&raw_time, &tm);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%SZ", &tm);
    return std::string(buffer);
}

void remove_product_block(std::string& text, const std::string& product) {
    const std::string product_table = "[products." + product + "]";
    const std::size_t table_pos = text.find(product_table);
    if (table_pos == std::string::npos) {
        return;
    }

    std::size_t start = text.rfind('\n', table_pos);
    start = start == std::string::npos ? 0 : start + 1;

    std::size_t end = text.find("\n[", table_pos + product_table.size());
    if (end == std::string::npos) {
        end = text.size();
    } else {
        end += 1;
    }

    text.erase(start, end - start);
}

std::optional<std::filesystem::path> upsert_project_gitignore(const std::filesystem::path& project_root) {
    const std::filesystem::path gitignore_path = project_root / ".gitignore";
    std::string text;
    if (std::filesystem::exists(gitignore_path)) {
        std::ifstream in(gitignore_path);
        if (!in.is_open()) {
            throw std::runtime_error("Failed to open " + gitignore_path.string() + " for reading");
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    bool changed = false;
    std::ostringstream additions;
    if (text.find("# Kano backlog cache and logs (derived data)") == std::string::npos) {
        additions << "# Kano backlog cache and logs (derived data)\n";
        changed = true;
    }
    if (text.find(".kano/cache") == std::string::npos) {
        additions << ".kano/cache/\n";
        changed = true;
    }
    if (text.find("_kano/backlog/_shared/logs") == std::string::npos) {
        additions << "_kano/backlog/_shared/logs/\n";
        changed = true;
    }
    if (!changed) {
        return std::nullopt;
    }

    std::ofstream out(gitignore_path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open " + gitignore_path.string() + " for writing");
    }
    if (!text.empty()) {
        out << trim(text) << "\n\n";
    }
    out << additions.str();
    return gitignore_path;
}

std::filesystem::path upsert_project_config(
    const std::filesystem::path& project_root,
    const std::string& product,
    const std::string& product_name,
    const std::string& prefix,
    const std::string& backlog_root,
    const std::string& agent,
    bool force,
    bool& created
) {
    const std::filesystem::path kano_dir = project_root / ".kano";
    std::filesystem::create_directories(kano_dir);
    const std::filesystem::path config_path = kano_dir / "backlog_config.toml";

    created = false;
    if (!std::filesystem::exists(config_path)) {
        std::ofstream initial(config_path);
        if (!initial.is_open()) {
            throw std::runtime_error("Failed to open " + config_path.string() + " for writing");
        }
        initial
            << "# Project-Level Backlog Configuration\n"
            << "# This file is source-of-truth and should be committed.\n\n"
            << "[defaults]\n"
            << "auto_refresh = true\n"
            << "skill_developer = false\n\n"
            << "[shared.cache]\n"
            << "root = \".kano/cache/backlog\"\n\n"
            << "[shared.vector]\n"
            << "enabled = true\n"
            << "backend = \"sqlite\"\n"
            << "path = \".kano/cache/backlog/vector\"\n"
            << "collection = \"backlog\"\n"
            << "metric = \"cosine\"\n";
        created = true;
    }

    std::ifstream in(config_path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open " + config_path.string() + " for reading");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();

    const std::string product_table = "[products." + product + "]";
    if (text.find(product_table) != std::string::npos && !force) {
        return config_path;
    }
    if (text.find(product_table) != std::string::npos && force) {
        remove_product_block(text, product);
    }

    std::ofstream out(config_path);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open " + config_path.string() + " for writing");
    }
    out << trim(text) << "\n\n"
        << "# Added by kano-backlog admin init (" << utc_timestamp() << ", agent=" << agent << ")\n"
        << product_table << "\n"
        << "name = " << toml_string(product_name) << "\n"
        << "prefix = " << toml_string(prefix) << "\n"
        << "backlog_root = " << toml_string(backlog_root) << "\n";
    return config_path;
}

} // namespace

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

bool debug_init_trace_enabled() {
    const char* value = std::getenv("KANO_BACKLOG_DEBUG_INIT");
    return value != nullptr && std::string(value) == "1";
}

void debug_init_trace(const char* message) {
    if (debug_init_trace_enabled()) {
        std::cerr << "[init-trace] " << message << "\n";
    }
}

} // namespace

OrchestrationOps::InitResult OrchestrationOps::initialize_backlog(const InitOptions& options) {
    debug_init_trace("enter initialize_backlog");
    const std::string agent = normalize_agent_id(options.agent);
    debug_init_trace("normalized agent");
    const std::string product = normalize_product_name(options.product);
    debug_init_trace("normalized product");
    const std::filesystem::path backlog_root = resolve_backlog_root(options);
    debug_init_trace("resolved backlog root");
    const std::filesystem::path project_root = resolve_project_root(backlog_root);
    debug_init_trace("resolved project root");
    const std::filesystem::path products_root = backlog_root / "products";
    const std::filesystem::path product_root = products_root / product;
    debug_init_trace("computed root paths");

    if (std::filesystem::exists(product_root) && !options.force) {
        throw std::runtime_error("Product backlog already exists: " + product_root.string() + " (use --force to update config/scaffold)");
    }
    debug_init_trace("checked existing product root");

    InitResult result;
    result.project_root = project_root;
    result.backlog_root = backlog_root;
    result.product_root = product_root;

    if (ensure_dir(backlog_root)) result.created_paths.push_back(backlog_root);
    if (ensure_dir(products_root)) result.created_paths.push_back(products_root);
    if (ensure_dir(product_root)) result.created_paths.push_back(product_root);

    const std::vector<std::filesystem::path> scaffold_dirs = {
        product_root / "decisions",
        product_root / "views",
        product_root / "items",
        product_root / "_meta",
        product_root / "artifacts",
        backlog_root / ".cache" / "index",
    };
    for (const auto& dir : scaffold_dirs) {
        if (ensure_dir(dir)) result.created_paths.push_back(dir);
    }
    for (const char* item_type : kItemTypes) {
        const std::filesystem::path type_dir = product_root / "items" / item_type;
        const std::filesystem::path bucket_dir = type_dir / "0000";
        if (ensure_dir(type_dir)) result.created_paths.push_back(type_dir);
        if (ensure_dir(bucket_dir)) result.created_paths.push_back(bucket_dir);
    }
    debug_init_trace("created scaffold directories");

    std::string actual_product_name = options.product_name ? trim(*options.product_name) : product;
    if (actual_product_name.empty()) {
        throw std::runtime_error("Product name cannot be empty");
    }
    std::string actual_prefix = options.prefix ? trim(*options.prefix) : derive_prefix(actual_product_name);
    if (actual_prefix.empty()) {
        throw std::runtime_error("Product prefix cannot be empty");
    }
    std::transform(actual_prefix.begin(), actual_prefix.end(), actual_prefix.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    debug_init_trace("resolved product display metadata");

    bool config_created = false;
    debug_init_trace("upserting project config");
    result.config_path = upsert_project_config(
        project_root,
        product,
        actual_product_name,
        actual_prefix,
        relativize(product_root, project_root),
        agent,
        options.force,
        config_created
    );
    if (config_created) {
        result.created_paths.push_back(result.config_path);
    }
    debug_init_trace("upserted project config");

    debug_init_trace("upserting project gitignore");
    if (const auto gitignore_path = upsert_project_gitignore(project_root)) {
        result.created_paths.push_back(*gitignore_path);
    }
    debug_init_trace("upserted project gitignore");

    if (options.refresh_views) {
        debug_init_trace("refreshing dashboards");
        result.views_refreshed = ViewOps::refresh_dashboards(product_root, agent).views_refreshed;
        debug_init_trace("refreshed dashboards");
    }

    debug_init_trace("leave initialize_backlog");
    return result;
}

void OrchestrationOps::refresh_index(BacklogIndex& index, const std::filesystem::path& root) {
    // Clear and rebuild index from files
    CanonicalStore store(root);
    auto item_paths = store.list_items();
    
    // In a real implementation, we'd clear the index first.
    for (const auto& path : item_paths) {
        try {
            auto item = store.read(path);
            index.index_item(item);
        } catch (...) {
            // Log and continue
        }
    }
}

} // namespace kano::backlog_ops
