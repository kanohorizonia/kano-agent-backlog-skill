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
}

}  // namespace

namespace kano::backlog_core {

namespace {

bool debug_config_trace_enabled() {
    const char* value = std::getenv("KANO_BACKLOG_DEBUG_CONFIG");
    return value != nullptr && std::string(value) == "1";
}

void debug_config_trace(const char* message) {
    if (debug_config_trace_enabled()) {
        std::cerr << "[config-trace] " << message << "\n";
    }
}

} // namespace

// ProjectConfig Implementation
std::optional<ProjectConfig> ProjectConfig::load_from_toml(const std::filesystem::path& file_path) {
    debug_config_trace("enter load_from_toml");
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

    debug_config_trace("leave load_from_toml");
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

// ConfigLoader Implementation
std::optional<std::filesystem::path> ConfigLoader::find_project_config(const std::filesystem::path& start_path) {
    std::filesystem::path current = std::filesystem::is_directory(start_path) ? start_path : start_path.parent_path();
    
    while (true) {
        std::filesystem::path config_path = current / ".kano" / "backlog_config.toml";
        if (std::filesystem::exists(config_path)) {
            return config_path;
        }
        
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
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
    debug_config_trace("enter BacklogContext::resolve");
    std::filesystem::path abs_resource = std::filesystem::absolute(resource_path);
    debug_config_trace("resolved absolute resource");
    
    auto config_path = ConfigLoader::find_project_config(abs_resource);
    debug_config_trace("searched project config");
    if (!config_path) {
        throw ConfigError("Project config required but not found. Create .kano/backlog_config.toml in project root.");
    }

    auto project_config = ProjectConfig::load_from_toml(*config_path);
    debug_config_trace("loaded project config");
    if (!project_config) {
        throw ConfigError("Failed to parse project config at " + config_path->string());
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
    debug_config_trace("resolved product name");

    auto product_root = project_config->resolve_backlog_root(product_name, *config_path);
    debug_config_trace("resolved product root");
    if (!product_root) {
        throw ConfigError("Product '" + product_name + "' not found in project config");
    }

    std::filesystem::path project_root = ConfigLoader::resolve_project_root(*config_path).value_or(infer_project_root(*config_path));
    debug_config_trace("resolved project root");

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

    if (sandbox_name && !sandbox_name->empty()) {
        ctx.sandbox_root = backlog_root.parent_path() / "backlog_sandbox" / *sandbox_name;
        ctx.is_sandbox = true;
    }

    debug_config_trace("leave BacklogContext::resolve");
    return ctx;
}

} // namespace kano::backlog_core
