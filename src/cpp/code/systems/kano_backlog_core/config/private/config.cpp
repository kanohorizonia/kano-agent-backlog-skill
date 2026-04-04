#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/models/errors.hpp"

#include <kano_config.h>

#include <cstdlib>

namespace {

using kano::backlog_core::ProductDefinition;
using kano::backlog_core::ProjectConfig;

std::optional<std::string> get_optional_string(KanoConfig cfg, const std::string& key) {
    if (const char* value = kano_config_get(cfg, key.c_str())) {
        return std::string(value);
    }
    return std::nullopt;
}

std::optional<bool> get_optional_bool(KanoConfig cfg, const std::string& key) {
    bool value = false;
    if (kano_config_get_bool(cfg, key.c_str(), &value)) {
        return value;
    }
    return std::nullopt;
}

std::optional<int> get_optional_int(KanoConfig cfg, const std::string& key) {
    int value = 0;
    if (kano_config_get_int(cfg, key.c_str(), &value)) {
        return value;
    }
    return std::nullopt;
}

std::filesystem::path infer_project_root(const std::filesystem::path& config_file_path) {
    if (config_file_path.parent_path().filename() == ".kano") {
        return config_file_path.parent_path().parent_path();
    }
    return config_file_path.parent_path();
}

}  // namespace

namespace kano::backlog_core {

// ProjectConfig Implementation
std::optional<ProjectConfig> ProjectConfig::load_from_toml(const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return std::nullopt;
    }

    KanoConfig cfg = kano_config_load(file_path.string().c_str());
    if (!cfg) {
        throw ConfigError("Failed to parse TOML from " + file_path.string());
    }

    ProjectConfig config;

    size_t product_count = 0;
    char** product_names = kano_config_list_children(cfg, "products", &product_count);
    for (size_t index = 0; index < product_count; ++index) {
        const std::string key = product_names[index];
        const std::string prefix = "products." + key + ".";

        ProductDefinition product;
        product.name = get_optional_string(cfg, prefix + "name").value_or(key);
        product.prefix = get_optional_string(cfg, prefix + "prefix").value_or("");
        product.backlog_root = get_optional_string(cfg, prefix + "backlog_root").value_or("");

        product.vector_enabled = get_optional_bool(cfg, prefix + "vector_enabled");
        product.vector_backend = get_optional_string(cfg, prefix + "vector_backend");
        product.vector_metric = get_optional_string(cfg, prefix + "vector_metric");
        product.analysis_llm_enabled = get_optional_bool(cfg, prefix + "analysis_llm_enabled");
        product.cache_root = get_optional_string(cfg, prefix + "cache_root");
        product.log_debug = get_optional_bool(cfg, prefix + "log_debug");
        product.log_verbosity = get_optional_string(cfg, prefix + "log_verbosity");
        product.embedding_provider = get_optional_string(cfg, prefix + "embedding_provider");
        product.embedding_model = get_optional_string(cfg, prefix + "embedding_model");
        product.embedding_dimension = get_optional_int(cfg, prefix + "embedding_dimension");
        product.chunking_target_tokens = get_optional_int(cfg, prefix + "chunking_target_tokens");
        product.chunking_max_tokens = get_optional_int(cfg, prefix + "chunking_max_tokens");
        product.tokenizer_adapter = get_optional_string(cfg, prefix + "tokenizer_adapter");
        product.tokenizer_model = get_optional_string(cfg, prefix + "tokenizer_model");

        config.products[key] = product;
    }

    kano_config_free_string_array(product_names, product_count);
    kano_config_free(cfg);
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

    return std::filesystem::weakly_canonical(project_root / backlog_root);
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
    KanoConfig cfg = kano_config_load(config_file_path.string().c_str());
    if (!cfg) {
        return std::nullopt;
    }

    const char* root_value = kano_config_root(cfg);
    std::optional<std::filesystem::path> root;
    if (root_value && *root_value) {
        root = std::filesystem::path(root_value);
    }
    kano_config_free(cfg);
    return root;
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

    if (sandbox_name && !sandbox_name->empty()) {
        ctx.sandbox_root = backlog_root.parent_path() / "backlog_sandbox" / *sandbox_name;
        ctx.is_sandbox = true;
    }

    return ctx;
}

} // namespace kano::backlog_core
