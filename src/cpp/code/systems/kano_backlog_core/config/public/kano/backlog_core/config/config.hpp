#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>
#include <memory>

// Forward declare toml::node to avoid bringing in the whole toml++ header in public API
namespace toml {
    inline namespace v3 {
        class table;
    }
}

namespace kano::backlog_core {

// Forward declarations
class BacklogContext;
class ProjectConfig;

struct ChunkingOptions {
    int target_tokens = 256;
    int max_tokens = 512;
    int overlap_tokens = 32;
    std::string version = "chunk-v1";
    std::string tokenizer_adapter = "auto";
};

struct TokenizerConfig {
    std::string adapter = "auto";
    std::string model = "text-embedding-3-small";
    std::optional<int> max_tokens;
    std::vector<std::string> fallback_chain;
    std::map<std::string, std::string> options; // simplified
};

struct EmbeddingConfig {
    std::string provider = "noop";
    std::string model = "noop-embedding";
    int dimension = 1536;
};

struct VectorConfig {
    bool enabled = false;
    std::string backend = "sqlite";
    std::string collection = "backlog";
    std::string metric = "cosine";
};

struct PipelineConfig {
    ChunkingOptions chunking;
    TokenizerConfig tokenizer;
    EmbeddingConfig embedding;
    VectorConfig vector;
};

struct ProductDefinition {
    std::string name;
    std::string prefix;
    std::string backlog_root;

    // Flattened overrides
    std::optional<bool> vector_enabled;
    std::optional<std::string> vector_backend;
    std::optional<std::string> vector_metric;
    std::optional<bool> analysis_llm_enabled;
    std::optional<std::string> cache_root;
    std::optional<bool> log_debug;
    std::optional<std::string> log_verbosity;
    std::optional<std::string> embedding_provider;
    std::optional<std::string> embedding_model;
    std::optional<int> embedding_dimension;
    std::optional<int> chunking_target_tokens;
    std::optional<int> chunking_max_tokens;
    std::optional<std::string> tokenizer_adapter;
    std::optional<std::string> tokenizer_model;
};

class ProjectConfig {
public:
    std::map<std::string, ProductDefinition> products;

    static std::optional<ProjectConfig> load_from_toml(const std::filesystem::path& file_path);
    std::optional<ProductDefinition> get_product(const std::string& name) const;
    std::optional<std::filesystem::path> resolve_backlog_root(const std::string& product_name, const std::filesystem::path& config_file_path) const;
};

class BacklogContext {
public:
    std::filesystem::path project_root;
    std::filesystem::path backlog_root;
    std::filesystem::path product_root;
    std::optional<std::filesystem::path> sandbox_root;
    std::string product_name;
    bool is_sandbox = false;
    
    ProductDefinition product_def;

    // Equivalent to from_path in python
    static BacklogContext resolve(
        const std::filesystem::path& resource_path, 
        const std::optional<std::string>& product_name, 
        const std::optional<std::string>& sandbox_name = std::nullopt
    );
};

class ConfigLoader {
public:
    static std::optional<std::filesystem::path> find_project_config(const std::filesystem::path& start_path);
    static std::optional<std::filesystem::path> resolve_project_root(const std::filesystem::path& config_file_path);
};

} // namespace kano::backlog_core
