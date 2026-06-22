#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/models/errors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string_view>

namespace {

using kano::backlog_core::ProductDefinition;
using kano::backlog_core::ProjectConfig;

std::filesystem::path infer_project_root(const std::filesystem::path& config_file_path) {
    if (config_file_path.parent_path().filename() == ".kano") {
        return config_file_path.parent_path().parent_path();
    }
    return config_file_path.parent_path();
}

std::string trim_copy_local(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string strip_inline_comment(const std::string& line) {
    bool in_string = false;
    char quote = '\0';
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote = ch;
            continue;
        }
        if (ch == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string parse_toml_string_value(std::string value) {
    value = trim_copy_local(value);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        const char quote = value.front();
        std::string out;
        bool escaped = false;
        for (std::size_t i = 1; i + 1 < value.size(); ++i) {
            const char ch = value[i];
            if (quote == '"' && escaped) {
                switch (ch) {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(ch); break;
                }
                escaped = false;
            } else if (quote == '"' && ch == '\\') {
                escaped = true;
            } else {
                out.push_back(ch);
            }
        }
        return out;
    }
    if (value == "null") {
        return {};
    }
    return value;
}

std::optional<bool> parse_toml_bool(std::string value) {
    value = trim_copy_local(value);
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<int> parse_toml_int(const std::string& value) {
    try {
        std::size_t parsed = 0;
        const int result = std::stoi(trim_copy_local(value), &parsed);
        return parsed > 0 ? std::optional<int>(result) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> parse_products_section(const std::string& section) {
    constexpr std::string_view prefix = "products.";
    if (!section.starts_with(prefix)) {
        return std::nullopt;
    }
    auto product = trim_copy_local(section.substr(prefix.size()));
    if (product.empty()) {
        return std::nullopt;
    }
    return parse_toml_string_value(product);
}

void apply_product_value(ProductDefinition& product, const std::string& key, const std::string& value) {
    if (key == "name") product.name = parse_toml_string_value(value);
    else if (key == "prefix") product.prefix = parse_toml_string_value(value);
    else if (key == "backlog_root") product.backlog_root = parse_toml_string_value(value);
    else if (key == "vector_enabled") product.vector_enabled = parse_toml_bool(value);
    else if (key == "vector_backend") product.vector_backend = parse_toml_string_value(value);
    else if (key == "vector_metric") product.vector_metric = parse_toml_string_value(value);
    else if (key == "analysis_llm_enabled") product.analysis_llm_enabled = parse_toml_bool(value);
    else if (key == "cache_root") product.cache_root = parse_toml_string_value(value);
    else if (key == "log_debug") product.log_debug = parse_toml_bool(value);
    else if (key == "log_verbosity") product.log_verbosity = parse_toml_string_value(value);
    else if (key == "embedding_provider") product.embedding_provider = parse_toml_string_value(value);
    else if (key == "embedding_model") product.embedding_model = parse_toml_string_value(value);
    else if (key == "embedding_dimension") product.embedding_dimension = parse_toml_int(value);
    else if (key == "chunking_target_tokens") product.chunking_target_tokens = parse_toml_int(value);
    else if (key == "chunking_max_tokens") product.chunking_max_tokens = parse_toml_int(value);
    else if (key == "tokenizer_adapter") product.tokenizer_adapter = parse_toml_string_value(value);
    else if (key == "tokenizer_model") product.tokenizer_model = parse_toml_string_value(value);
    else if (key == "default_assignee") product.default_assignee = parse_toml_string_value(value);
    else if (key == "default_bug_reviewer") product.default_bug_reviewer = parse_toml_string_value(value);
}

void apply_product_local_config_file(ProductDefinition& product, const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return;
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        throw kano::backlog_core::ConfigError("Failed to read product TOML from " + file_path.string());
    }

    bool in_product_section = false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_copy_local(strip_inline_comment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            in_product_section = trim_copy_local(line.substr(1, line.size() - 2)) == "product";
            continue;
        }
        if (!in_product_section) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = trim_copy_local(line.substr(0, eq));
        const auto value = trim_copy_local(line.substr(eq + 1));
        apply_product_value(product, key, value);
    }
}

std::string normalize_product_prefix(std::string prefix) {
    prefix = trim_copy_local(prefix);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return prefix;
}

std::string display_path(const std::filesystem::path& path) {
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
    return normalized.string();
}

}  // namespace

namespace kano::backlog_core {

// ProjectConfig Implementation
std::optional<ProjectConfig> ProjectConfig::load_from_toml(const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return std::nullopt;
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        throw ConfigError("Failed to read TOML from " + file_path.string());
    }

    ProjectConfig config;
    std::optional<std::string> current_product;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_copy_local(strip_inline_comment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            current_product = parse_products_section(trim_copy_local(line.substr(1, line.size() - 2)));
            if (current_product) {
                auto& product = config.products[*current_product];
                if (product.name.empty()) {
                    product.name = *current_product;
                }
            }
            continue;
        }
        if (!current_product) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = trim_copy_local(line.substr(0, eq));
        const auto value = trim_copy_local(line.substr(eq + 1));
        apply_product_value(config.products[*current_product], key, value);
    }

    return config;
}

std::optional<ProductDefinition> ProjectConfig::get_product(const std::string& name) const {
    auto it = products.find(name);
    if (it != products.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ProjectConfig::resolve_backlog_root(const std::string& product_name, const std::filesystem::path& config_file_path) const {
    auto product = get_product(product_name);
    if (!product) {
        return std::nullopt;
    }

    std::filesystem::path backlog_root(product->backlog_root);
    if (backlog_root.is_absolute()) {
        return backlog_root;
    }

    std::filesystem::path project_root = infer_project_root(config_file_path);

    std::error_code ec;
    auto resolved = std::filesystem::weakly_canonical(project_root / backlog_root, ec);
    if (ec) {
        resolved = std::filesystem::absolute(project_root / backlog_root, ec).lexically_normal();
    }
    return resolved;
}

std::vector<ProductPrefixCollision> ProjectConfig::find_prefix_collisions(const std::filesystem::path& config_file_path) const {
    std::vector<ProductPrefixCollision> collisions;
    std::map<std::string, std::vector<std::string>> products_by_prefix;

    for (const auto& [product_name, definition] : products) {
        const auto normalized_prefix = normalize_product_prefix(definition.prefix);
        if (normalized_prefix.empty()) {
            continue;
        }
        products_by_prefix[normalized_prefix].push_back(product_name);
    }

    const auto config_display = display_path(config_file_path);
    for (const auto& [prefix, product_names] : products_by_prefix) {
        if (product_names.size() < 2) {
            continue;
        }
        for (std::size_t left_index = 0; left_index < product_names.size(); ++left_index) {
            for (std::size_t right_index = left_index + 1; right_index < product_names.size(); ++right_index) {
                const auto& left_product = product_names[left_index];
                const auto& right_product = product_names[right_index];
                const auto left_definition = products.at(left_product);
                const auto right_definition = products.at(right_product);
                ProductPrefixCollision collision;
                collision.prefix = prefix;
                collision.left_product = left_product;
                collision.left_prefix = trim_copy_local(left_definition.prefix);
                collision.left_config_path = config_display;
                if (const auto root = resolve_backlog_root(left_product, config_file_path)) {
                    collision.left_backlog_root = display_path(*root);
                }
                collision.right_product = right_product;
                collision.right_prefix = trim_copy_local(right_definition.prefix);
                collision.right_config_path = config_display;
                if (const auto root = resolve_backlog_root(right_product, config_file_path)) {
                    collision.right_backlog_root = display_path(*root);
                }
                collisions.push_back(collision);
            }
        }
    }

    return collisions;
}

std::string ProjectConfig::describe_prefix_collision(const ProductPrefixCollision& collision) {
    std::ostringstream out;
    out << "Product prefix collision: normalized prefix " << collision.prefix
        << " is shared by product " << collision.left_product
        << " (prefix=" << collision.left_prefix
        << ", config=" << collision.left_config_path;
    if (!collision.left_backlog_root.empty()) {
        out << ", backlog_root=" << collision.left_backlog_root;
    }
    out << ") and product " << collision.right_product
        << " (prefix=" << collision.right_prefix
        << ", config=" << collision.right_config_path;
    if (!collision.right_backlog_root.empty()) {
        out << ", backlog_root=" << collision.right_backlog_root;
    }
    out << ")";
    return out.str();
}

std::string ProjectConfig::describe_prefix_collisions(const std::vector<ProductPrefixCollision>& collisions) {
    std::ostringstream out;
    for (std::size_t index = 0; index < collisions.size(); ++index) {
        if (index > 0) {
            out << "\n";
        }
        out << describe_prefix_collision(collisions[index]);
    }
    return out.str();
}

// ConfigLoader Implementation
std::vector<std::filesystem::path> ConfigLoader::project_config_candidates(const std::filesystem::path& start_path) {
    std::error_code ec;
    std::filesystem::path current = std::filesystem::is_directory(start_path, ec) ? start_path : start_path.parent_path();
    if (current.empty()) {
        current = ".";
    }
    current = std::filesystem::absolute(current, ec);
    if (ec) {
        current = start_path.lexically_normal();
        ec.clear();
    }
    current = current.lexically_normal();

    std::vector<std::filesystem::path> candidates;
    std::set<std::string> seen;
    auto add_candidate = [&](const std::filesystem::path& candidate) {
        const auto normalized = candidate.lexically_normal();
        const auto key = normalized.string();
        if (seen.insert(key).second) {
            candidates.push_back(normalized);
        }
    };

    while (true) {
        add_candidate(current / ".kano" / "backlog_config.toml");
        add_candidate(current / "_kano" / "backlog" / ".kano" / "backlog_config.toml");

        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return candidates;
}

std::optional<std::filesystem::path> ConfigLoader::find_project_config(const std::filesystem::path& start_path) {
    for (const auto& config_path : project_config_candidates(start_path)) {
        if (std::filesystem::exists(config_path)) {
            return config_path;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ConfigLoader::resolve_project_root(const std::filesystem::path& config_file_path) {
    std::error_code ec;
    auto root = infer_project_root(config_file_path);
    auto normalized = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        ec.clear();
        normalized = std::filesystem::absolute(root, ec).lexically_normal();
        if (ec) {
            normalized = root.lexically_normal();
        }
    }
    if (normalized.filename().empty()) {
        normalized = normalized.parent_path();
    }
    return normalized;
}

// BacklogContext Implementation
BacklogContext BacklogContext::resolve(
    const std::filesystem::path& resource_path, 
    const std::optional<std::string>& product_name_opt, 
    const std::optional<std::string>& sandbox_name
) {
    std::filesystem::path abs_resource = std::filesystem::absolute(resource_path);
    
    auto config_path = ConfigLoader::find_project_config(abs_resource);
    if (!config_path) {
        throw ConfigError("Project config required but not found. Create .kano/backlog_config.toml in project root.");
    }

    auto project_config = ProjectConfig::load_from_toml(*config_path);
    if (!project_config) {
        throw ConfigError("Failed to parse project config at " + config_path->string());
    }
    if (const auto collisions = project_config->find_prefix_collisions(*config_path); !collisions.empty()) {
        throw ConfigError(ProjectConfig::describe_prefix_collisions(collisions));
    }

    std::string product_name;
    bool is_sandbox = false;

    // The effective product definition resolved for this context
    ProductDefinition product_def;
    if (!product_name_opt || product_name_opt->empty()) {
        if (project_config->products.size() == 1) {
            product_name = project_config->products.begin()->first;
        } else if (project_config->products.size() > 1) {
            throw ConfigError("Multiple products found; specify product explicitly.");
        } else {
            throw ConfigError("No products defined in project config");
        }
    } else {
        product_name = *product_name_opt;
    }

    auto product_root = project_config->resolve_backlog_root(product_name, *config_path);
    if (!product_root) {
        throw ConfigError("Product '" + product_name + "' not found in project config");
    }

    std::filesystem::path project_root = ConfigLoader::resolve_project_root(*config_path).value_or(infer_project_root(*config_path));

    std::filesystem::path backlog_root = *product_root;
    if (product_root->parent_path().filename() == "products" && product_root->parent_path().parent_path().filename() == "backlog") {
        backlog_root = product_root->parent_path().parent_path();
    }

    BacklogContext ctx;
    ctx.project_root = project_root;
    ctx.product_root = *product_root;
    ctx.backlog_root = backlog_root;
    ctx.product_name = product_name;
    
    // Find the actual product definition from config
    auto it = project_config->products.find(product_name);
    if (it != project_config->products.end()) {
        ctx.product_def = it->second;
    }
    apply_product_local_config_file(
        ctx.product_def,
        ctx.product_root / "_config" / "config.toml"
    );

    if (sandbox_name && !sandbox_name->empty()) {
        ctx.sandbox_root = backlog_root.parent_path() / "backlog_sandbox" / *sandbox_name;
        ctx.is_sandbox = true;
    }

    return ctx;
}

} // namespace kano::backlog_core
