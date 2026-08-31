#include "kano/backlog_ops/orchestration/orchestration_ops.hpp"
#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kItemTypes[] = {"initiative", "epic", "feature", "userstory", "task", "subtask", "bug", "issue"};

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

std::vector<std::string> product_name_segments(std::string product_name) {
    product_name = trim(product_name);
    std::transform(product_name.begin(), product_name.end(), product_name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    std::vector<std::string> segments;
    std::string current;
    for (const unsigned char ch : product_name) {
        if (std::isalnum(ch)) {
            current.push_back(static_cast<char>(ch));
        } else if (!current.empty()) {
            segments.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

bool is_valid_prefix_candidate(const std::string& value) {
    return value.size() >= 2 && value.size() <= 16 &&
           std::isalpha(static_cast<unsigned char>(value.front())) &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isalnum(ch); });
}

struct DerivedPrefixSelection {
    std::string prefix;
    std::vector<std::string> considered;
};

DerivedPrefixSelection select_derived_prefix(
    const std::string& product_name,
    const std::set<std::string>& occupied_prefixes
) {
    DerivedPrefixSelection result;
    std::set<std::string> emitted;

    const auto try_candidate = [&](std::string candidate) {
        if (candidate.size() > 16) {
            candidate.resize(16);
        }
        if (!is_valid_prefix_candidate(candidate) || !emitted.insert(candidate).second) {
            return false;
        }
        result.considered.push_back(candidate);
        if (!occupied_prefixes.contains(candidate)) {
            result.prefix = candidate;
            return true;
        }
        return false;
    };

    const std::string primary = derive_prefix(product_name);
    if (try_candidate(primary)) {
        return result;
    }

    const auto segments = product_name_segments(product_name);
    std::string acronym;
    std::string compact;
    for (const auto& segment : segments) {
        if (!segment.empty()) {
            acronym.push_back(segment.front());
            compact += segment;
        }
    }
    if (try_candidate(acronym) || try_candidate(compact)) {
        return result;
    }

    const std::string suffix_base = is_valid_prefix_candidate(primary) ? primary : "PX";
    for (int suffix = 2; suffix <= 9999; ++suffix) {
        const std::string suffix_text = std::to_string(suffix);
        const auto base_size = std::min<std::size_t>(suffix_base.size(), 16 - suffix_text.size());
        if (try_candidate(suffix_base.substr(0, base_size) + suffix_text)) {
            return result;
        }
    }

    throw std::runtime_error("Unable to derive an unused product prefix; pass an explicit --prefix");
}

std::string normalize_prefix(std::string prefix) {
    prefix = trim(prefix);
    if (prefix.empty()) {
        throw std::runtime_error("Product prefix cannot be empty");
    }
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (prefix.size() < 2 || prefix.size() > 16 || !std::isalpha(static_cast<unsigned char>(prefix.front())) ||
        !std::all_of(prefix.begin(), prefix.end(), [](unsigned char ch) { return std::isalnum(ch); })) {
        throw std::runtime_error("Product prefix must be 2-16 ASCII letters or digits, start with a letter, and contain no separators");
    }
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

std::filesystem::path normalize_path_for_bounds(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized.lexically_normal();
    }
    return std::filesystem::absolute(path).lexically_normal();
}

bool is_within(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const std::filesystem::path rel = child.lexically_relative(parent);
    return !rel.empty() && rel.generic_string().find("..") != 0 && !rel.is_absolute();
}

std::filesystem::path resolve_backlog_root(const kano::backlog_ops::OrchestrationOps::InitOptions& options) {
    const std::filesystem::path start = normalize_path_for_bounds(options.start_path);
    const std::filesystem::path project_root = normalize_path_for_bounds(find_repo_root(start));
    if (options.backlog_root) {
        std::filesystem::path root = options.backlog_root->is_absolute()
            ? *options.backlog_root
            : project_root / *options.backlog_root;
        root = normalize_path_for_bounds(root);
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
    if (std::filesystem::exists(backlog_root / ".git")) {
        return backlog_root;
    }
    if (backlog_root.parent_path().filename() == "_kano") {
        return backlog_root.parent_path().parent_path();
    }
    return backlog_root.parent_path();
}

std::vector<std::filesystem::path> planned_scaffold_directories(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& products_root,
    const std::filesystem::path& product_root
) {
    std::vector<std::filesystem::path> directories = {
        backlog_root,
        products_root,
        product_root,
        product_root / "decisions",
        product_root / "views",
        product_root / "items",
        product_root / "_meta",
        product_root / "artifacts",
        backlog_root / ".cache" / "index",
    };
    for (const char* item_type : kItemTypes) {
        const std::filesystem::path type_dir = product_root / "items" / item_type;
        directories.push_back(type_dir);
        directories.push_back(type_dir / "0000");
    }
    return directories;
}

std::vector<std::filesystem::path> planned_scaffold_files(const std::filesystem::path& project_root) {
    return {
        project_root / ".kano" / "backlog_config.toml",
        project_root / ".gitignore",
    };
}

std::vector<std::string> normalize_selector_list(
    const std::vector<std::string>& selectors,
    const std::string& selector_kind
) {
    std::vector<std::string> normalized;
    normalized.reserve(selectors.size());
    for (const auto& selector : selectors) {
        auto value = kano::backlog_core::ProjectConfig::normalize_product_selector(selector);
        if (value.empty()) {
            throw std::runtime_error(selector_kind + " cannot normalize to empty");
        }
        normalized.push_back(std::move(value));
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

void validate_selector_collisions(
    const std::optional<kano::backlog_core::ProjectConfig>& existing_config,
    const std::string& product,
    const kano::backlog_core::ProductDefinition& product_definition
) {
    kano::backlog_core::ProjectConfig prospective = existing_config.value_or(
        kano::backlog_core::ProjectConfig{});
    prospective.products[product] = product_definition;

    std::vector<kano::backlog_core::ProductSelectorCollision> product_collisions;
    for (const auto& collision : prospective.find_selector_collisions()) {
        const bool involves_product = std::any_of(
            collision.claims.begin(), collision.claims.end(),
            [&](const auto& claim) { return claim.canonical_slug == product; });
        const bool involves_other_product = std::any_of(
            collision.claims.begin(), collision.claims.end(),
            [&](const auto& claim) { return claim.canonical_slug != product; });
        if (involves_product && involves_other_product) {
            product_collisions.push_back(collision);
        }
    }
    if (!product_collisions.empty()) {
        throw std::runtime_error(
            kano::backlog_core::ProjectConfig::describe_selector_collisions(
                product_collisions));
    }
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

std::string toml_string_array(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << toml_string(values[index]);
    }
    out << ']';
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
    const kano::backlog_core::ProductDefinition& product_definition,
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
        << "name = " << toml_string(product_definition.name) << "\n"
        << "prefix = " << toml_string(product_definition.prefix) << "\n"
        << "backlog_root = " << toml_string(product_definition.backlog_root) << "\n";
    if (!product_definition.aliases.empty()) {
        out << "aliases = " << toml_string_array(product_definition.aliases) << "\n";
    }
    if (!product_definition.repo_bindings.empty()) {
        out << "repo_bindings = " << toml_string_array(product_definition.repo_bindings) << "\n";
    }
    return config_path;
}

} // namespace

namespace kano::backlog_ops {

using namespace kano::backlog_core;

OrchestrationOps::InitResult OrchestrationOps::initialize_backlog(const InitOptions& options) {
    const std::string agent = normalize_agent_id(options.agent);
    const std::filesystem::path backlog_root = resolve_backlog_root(options);
    const std::filesystem::path project_root = resolve_project_root(backlog_root);
    const std::filesystem::path config_path = project_root / ".kano" / "backlog_config.toml";
    std::optional<kano::backlog_core::ProjectConfig> existing_config;
    if (std::filesystem::exists(config_path)) {
        existing_config = kano::backlog_core::ProjectConfig::load_from_toml(config_path);
    }

    std::string product;
    if (existing_config) {
        if (const auto resolution = existing_config->resolve_product(options.product)) {
            product = resolution->canonical_slug;
        }
    }
    if (product.empty()) {
        product = normalize_product_name(options.product);
    }

    const std::filesystem::path products_root = backlog_root / "products";
    const std::filesystem::path product_root = products_root / product;
    if (std::filesystem::exists(product_root) && !options.force) {
        throw std::runtime_error("Product backlog already exists: " + product_root.string() + " (use --force to update config/scaffold)");
    }

    std::optional<kano::backlog_core::ProductDefinition> existing_product;
    if (existing_config) {
        existing_product = existing_config->get_product(product);
    }

    std::string actual_product_name = options.product_name ? trim(*options.product_name) : product;
    if (actual_product_name.empty()) {
        throw std::runtime_error("Product name cannot be empty");
    }

    std::optional<std::string> existing_product_prefix;
    if (existing_product) {
        if (!trim(existing_product->prefix).empty()) {
            existing_product_prefix = existing_product->prefix;
        }
    }

    std::string actual_prefix;
    std::string prefix_source;
    std::vector<std::string> prefix_candidates;
    if (options.prefix) {
        actual_prefix = normalize_prefix(*options.prefix);
        prefix_source = "explicit";
        prefix_candidates.push_back(actual_prefix);
    } else if (existing_product_prefix) {
        actual_prefix = normalize_prefix(*existing_product_prefix);
        prefix_source = "existing";
        prefix_candidates.push_back(actual_prefix);
    } else {
        std::set<std::string> occupied_prefixes;
        if (existing_config) {
            for (const auto& [configured_product, definition] : existing_config->products) {
                if (configured_product != product && !trim(definition.prefix).empty()) {
                    occupied_prefixes.insert(normalize_prefix(definition.prefix));
                }
            }
        }
        auto selection = select_derived_prefix(actual_product_name, occupied_prefixes);
        actual_prefix = selection.prefix;
        prefix_candidates = std::move(selection.considered);
        prefix_source = prefix_candidates.size() == 1 ? "derived" : "derived_collision_free";
    }

    std::vector<std::string> alias_candidates = options.aliases;
    std::vector<std::string> repo_binding_candidates = options.repo_bindings;
    if (alias_candidates.empty() && existing_product) {
        alias_candidates = existing_product->aliases;
    }
    if (repo_binding_candidates.empty() && existing_product) {
        repo_binding_candidates = existing_product->repo_bindings;
    }
    auto aliases = normalize_selector_list(alias_candidates, "Product alias selector");
    auto repo_bindings = normalize_selector_list(
        repo_binding_candidates, "Product repository binding selector");

    kano::backlog_core::ProductDefinition product_definition =
        existing_product.value_or(kano::backlog_core::ProductDefinition{});
    product_definition.name = actual_product_name;
    product_definition.prefix = actual_prefix;
    product_definition.backlog_root = relativize(product_root, project_root);
    product_definition.aliases = aliases;
    product_definition.repo_bindings = repo_bindings;
    validate_selector_collisions(existing_config, product, product_definition);

    InitResult result;
    result.status = options.dry_run ? "dry-run" : "initialized";
    result.product = product;
    result.product_name = actual_product_name;
    result.prefix = actual_prefix;
    result.aliases = aliases;
    result.repo_bindings = repo_bindings;
    result.prefix_source = prefix_source;
    result.prefix_candidates = std::move(prefix_candidates);
    result.dry_run = options.dry_run;
    result.project_root = project_root;
    result.backlog_root = backlog_root;
    result.product_root = product_root;
    result.config_path = config_path;
    result.planned_directories = planned_scaffold_directories(backlog_root, products_root, product_root);
    result.planned_files = planned_scaffold_files(project_root);

    if (options.dry_run) {
        return result;
    }

    if (ensure_dir(backlog_root)) result.created_paths.push_back(backlog_root);
    if (ensure_dir(products_root)) result.created_paths.push_back(products_root);
    if (ensure_dir(product_root)) result.created_paths.push_back(product_root);

    for (const auto& dir : result.planned_directories) {
        if (ensure_dir(dir)) result.created_paths.push_back(dir);
    }

    bool config_created = false;
    result.config_path = upsert_project_config(
        project_root,
        product,
        product_definition,
        agent,
        options.force,
        config_created
    );
    if (config_created) {
        result.created_paths.push_back(result.config_path);
    }

    if (const auto gitignore_path = upsert_project_gitignore(project_root)) {
        result.created_paths.push_back(*gitignore_path);
    }

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
