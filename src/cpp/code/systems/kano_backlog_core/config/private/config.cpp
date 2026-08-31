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
#include <tuple>
#include <utility>

namespace {

using kano::backlog_core::ProductDefinition;
using kano::backlog_core::ProductResolutionKind;
using kano::backlog_core::ProjectConfig;

bool is_ascii_whitespace(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
        ch == '\f' || ch == '\v';
}

int resolution_precedence(ProductResolutionKind kind) {
    switch (kind) {
        case ProductResolutionKind::CanonicalSlug: return 0;
        case ProductResolutionKind::Prefix: return 1;
        case ProductResolutionKind::DisplayName: return 2;
        case ProductResolutionKind::RepoBinding: return 3;
        case ProductResolutionKind::ExplicitAlias: return 4;
    }
    return 5;
}

std::string bounded_diagnostic_value(std::string_view value) {
    constexpr std::size_t kMaximumBytes = 96;
    std::string bounded;
    bounded.reserve(std::min(value.size(), kMaximumBytes) + 3);
    const auto byte_count = std::min(value.size(), kMaximumBytes);
    for (std::size_t index = 0; index < byte_count; ++index) {
        const auto ch = static_cast<unsigned char>(value[index]);
        switch (ch) {
            case '\n': bounded += "\\n"; break;
            case '\r': bounded += "\\r"; break;
            case '\t': bounded += "\\t"; break;
            case '\\': bounded += "\\\\"; break;
            case '\'': bounded += "\\'"; break;
            default:
                bounded.push_back(ch < 0x20 || ch == 0x7f ? '?' : static_cast<char>(ch));
                break;
        }
    }
    if (value.size() > kMaximumBytes) {
        bounded += "...";
    }
    return bounded;
}

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

[[noreturn]] void throw_invalid_string_array(const std::string& value) {
    throw kano::backlog_core::ConfigError(
        "Invalid TOML string array: " + bounded_diagnostic_value(value));
}

std::vector<std::string> parse_toml_string_array(std::string value) {
    value = trim_copy_local(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw_invalid_string_array(value);
    }

    const std::size_t end = value.size() - 1;
    std::size_t position = 1;
    const auto skip_whitespace = [&]() {
        while (position < end && is_ascii_whitespace(static_cast<unsigned char>(value[position]))) {
            ++position;
        }
    };

    std::vector<std::string> values;
    skip_whitespace();
    while (position < end) {
        const char quote = value[position];
        if (quote != '"' && quote != '\'') {
            throw_invalid_string_array(value);
        }
        ++position;

        std::string parsed;
        bool closed = false;
        while (position < end) {
            const char ch = value[position++];
            if (ch == quote) {
                closed = true;
                break;
            }
            if (quote == '"' && ch == '\\') {
                if (position >= end) {
                    throw_invalid_string_array(value);
                }
                switch (value[position++]) {
                    case 'b': parsed.push_back('\b'); break;
                    case 't': parsed.push_back('\t'); break;
                    case 'n': parsed.push_back('\n'); break;
                    case 'f': parsed.push_back('\f'); break;
                    case 'r': parsed.push_back('\r'); break;
                    case '"': parsed.push_back('"'); break;
                    case '\\': parsed.push_back('\\'); break;
                    default: throw_invalid_string_array(value);
                }
                continue;
            }
            parsed.push_back(ch);
        }
        if (!closed) {
            throw_invalid_string_array(value);
        }
        values.push_back(std::move(parsed));

        skip_whitespace();
        if (position == end) {
            break;
        }
        if (value[position] != ',') {
            throw_invalid_string_array(value);
        }
        ++position;
        skip_whitespace();
        if (position == end) {
            throw_invalid_string_array(value);
        }
    }
    return values;
}

std::string parse_topics_date_prefix_policy(const std::string& value) {
    auto parsed = parse_toml_string_value(value);
    std::transform(parsed.begin(), parsed.end(), parsed.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (parsed != "off" && parsed != "warn" && parsed != "enforce") {
        throw kano::backlog_core::ConfigError(
            "topics_date_prefix_policy must be one of: off, warn, enforce"
        );
    }
    return parsed;
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
    auto parsed_product = parse_toml_string_value(product);
    if (ProjectConfig::normalize_product_selector(parsed_product).empty()) {
        throw kano::backlog_core::ConfigError(
            "Canonical product selector cannot normalize to empty");
    }
    return parsed_product;
}

void apply_product_value(ProductDefinition& product, const std::string& key, const std::string& value) {
    if (key == "name") product.name = parse_toml_string_value(value);
    else if (key == "prefix") product.prefix = parse_toml_string_value(value);
    else if (key == "backlog_root") product.backlog_root = parse_toml_string_value(value);
    else if (key == "aliases") product.aliases = parse_toml_string_array(value);
    else if (key == "repo_bindings") product.repo_bindings = parse_toml_string_array(value);
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
    else if (key == "topics_date_prefix_policy") product.topics_date_prefix_policy = parse_topics_date_prefix_policy(value);
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
        if (key == "aliases" || key == "repo_bindings") {
            continue;
        }
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

std::string to_string(ProductResolutionKind kind) {
    switch (kind) {
        case ProductResolutionKind::CanonicalSlug: return "canonical_slug";
        case ProductResolutionKind::Prefix: return "prefix";
        case ProductResolutionKind::DisplayName: return "display_name";
        case ProductResolutionKind::RepoBinding: return "repo_binding";
        case ProductResolutionKind::ExplicitAlias: return "explicit_alias";
    }
    return "unknown";
}

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

std::string ProjectConfig::normalize_product_selector(const std::string& selector) {
    std::size_t begin = 0;
    while (begin < selector.size() &&
           is_ascii_whitespace(static_cast<unsigned char>(selector[begin]))) {
        ++begin;
    }
    std::size_t end = selector.size();
    while (end > begin &&
           is_ascii_whitespace(static_cast<unsigned char>(selector[end - 1]))) {
        --end;
    }

    std::string normalized;
    normalized.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        const auto ch = static_cast<unsigned char>(selector[index]);
        normalized.push_back(ch >= 'A' && ch <= 'Z'
            ? static_cast<char>(ch + ('a' - 'A'))
            : static_cast<char>(ch));
    }
    return normalized;
}

std::vector<ProductSelectorClaim> ProjectConfig::selector_claims() const {
    std::map<std::pair<std::string, std::string>, ProductSelectorClaim> claims_by_owner;
    const auto add_claim = [&](const std::string& canonical_slug,
                               const std::string& selector,
                               ProductResolutionKind resolution_kind) {
        const auto normalized = normalize_product_selector(selector);
        if (normalized.empty()) {
            return;
        }

        ProductSelectorClaim claim{canonical_slug, resolution_kind, selector, normalized};
        const auto key = std::make_pair(normalized, canonical_slug);
        const auto existing = claims_by_owner.find(key);
        if (existing == claims_by_owner.end() ||
            resolution_precedence(resolution_kind) < resolution_precedence(existing->second.resolution_kind) ||
            (resolution_kind == existing->second.resolution_kind && selector < existing->second.selector)) {
            claims_by_owner[key] = std::move(claim);
        }
    };

    for (const auto& [canonical_slug, definition] : products) {
        add_claim(canonical_slug, canonical_slug, ProductResolutionKind::CanonicalSlug);
        add_claim(canonical_slug, definition.prefix, ProductResolutionKind::Prefix);
        add_claim(canonical_slug, definition.name, ProductResolutionKind::DisplayName);
        for (const auto& binding : definition.repo_bindings) {
            add_claim(canonical_slug, binding, ProductResolutionKind::RepoBinding);
        }
        for (const auto& alias : definition.aliases) {
            add_claim(canonical_slug, alias, ProductResolutionKind::ExplicitAlias);
        }
    }

    std::vector<ProductSelectorClaim> claims;
    claims.reserve(claims_by_owner.size());
    for (auto& [key, claim] : claims_by_owner) {
        static_cast<void>(key);
        claims.push_back(std::move(claim));
    }
    return claims;
}

std::optional<ProductResolution> ProjectConfig::resolve_product(const std::string& selector) const {
    const auto normalized = normalize_product_selector(selector);
    if (normalized.empty()) {
        return std::nullopt;
    }

    std::vector<ProductSelectorClaim> matches;
    for (const auto& claim : selector_claims()) {
        if (claim.normalized_selector == normalized) {
            matches.push_back(claim);
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() > 1) {
        throw ConfigError(describe_selector_collision(
            ProductSelectorCollision{normalized, std::move(matches)}));
    }

    const auto& match = matches.front();
    return ProductResolution{
        match.canonical_slug,
        match.resolution_kind,
        selector,
        normalized,
        match.selector,
    };
}

std::optional<std::string> ProjectConfig::resolve_product_name(const std::string& name_or_prefix) const {
    const auto resolution = resolve_product(name_or_prefix);
    return resolution
        ? std::optional<std::string>(resolution->canonical_slug)
        : std::nullopt;
}

std::optional<std::filesystem::path> ProjectConfig::resolve_backlog_root(const std::string& product_name, const std::filesystem::path& config_file_path) const {
    const auto resolution = resolve_product(product_name);
    if (!resolution) {
        return std::nullopt;
    }

    return resolve_backlog_root(*resolution, config_file_path);
}

std::optional<std::filesystem::path> ProjectConfig::resolve_backlog_root(const ProductResolution& resolution, const std::filesystem::path& config_file_path) const {
    auto product = get_product(resolution.canonical_slug);
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

std::vector<ProductSelectorCollision> ProjectConfig::find_selector_collisions() const {
    std::map<std::string, std::vector<ProductSelectorClaim>> claims_by_selector;
    for (auto claim : selector_claims()) {
        claims_by_selector[claim.normalized_selector].push_back(std::move(claim));
    }

    std::vector<ProductSelectorCollision> collisions;
    for (auto& [normalized_selector, claims] : claims_by_selector) {
        if (claims.size() > 1) {
            collisions.push_back(ProductSelectorCollision{
                normalized_selector,
                std::move(claims),
            });
        }
    }
    return collisions;
}

std::string ProjectConfig::describe_selector_collision(const ProductSelectorCollision& collision) {
    auto claims = collision.claims;
    std::sort(claims.begin(), claims.end(), [](const auto& left, const auto& right) {
        return std::tie(left.canonical_slug, left.resolution_kind, left.selector) <
            std::tie(right.canonical_slug, right.resolution_kind, right.selector);
    });

    constexpr std::size_t kMaximumClaims = 8;
    std::ostringstream out;
    out << "Product selector collision: normalized selector '"
        << bounded_diagnostic_value(collision.normalized_selector)
        << "' is claimed by multiple products";
    const auto claim_count = std::min(claims.size(), kMaximumClaims);
    for (std::size_t index = 0; index < claim_count; ++index) {
        const auto& claim = claims[index];
        out << (index == 0 ? ": " : ", ")
            << bounded_diagnostic_value(claim.canonical_slug)
            << " (kind=" << to_string(claim.resolution_kind)
            << ", selector='" << bounded_diagnostic_value(claim.selector) << "')";
    }
    if (claims.size() > claim_count) {
        out << ", ... " << (claims.size() - claim_count) << " additional claims omitted";
    }
    return out.str();
}

std::string ProjectConfig::describe_selector_collisions(const std::vector<ProductSelectorCollision>& collisions) {
    auto ordered = collisions;
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.normalized_selector < right.normalized_selector;
    });

    constexpr std::size_t kMaximumCollisions = 16;
    std::ostringstream out;
    const auto collision_count = std::min(ordered.size(), kMaximumCollisions);
    for (std::size_t index = 0; index < collision_count; ++index) {
        if (index > 0) {
            out << '\n';
        }
        out << describe_selector_collision(ordered[index]);
    }
    if (ordered.size() > collision_count) {
        if (collision_count > 0) {
            out << '\n';
        }
        out << "... " << (ordered.size() - collision_count)
            << " additional product selector collisions omitted";
    }
    return out.str();
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
                const ProductResolution left_resolution{
                    left_product,
                    ProductResolutionKind::CanonicalSlug,
                    left_product,
                    normalize_product_selector(left_product),
                    left_product,
                };
                if (const auto root = resolve_backlog_root(left_resolution, config_file_path)) {
                    collision.left_backlog_root = display_path(*root);
                }
                collision.right_product = right_product;
                collision.right_prefix = trim_copy_local(right_definition.prefix);
                collision.right_config_path = config_display;
                const ProductResolution right_resolution{
                    right_product,
                    ProductResolutionKind::CanonicalSlug,
                    right_product,
                    normalize_product_selector(right_product),
                    right_product,
                };
                if (const auto root = resolve_backlog_root(right_resolution, config_file_path)) {
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

    ProductResolution product_resolution;

    if (!product_name_opt || product_name_opt->empty()) {
        if (project_config->products.size() == 1) {
            const auto& canonical_slug = project_config->products.begin()->first;
            product_resolution = ProductResolution{
                canonical_slug,
                ProductResolutionKind::CanonicalSlug,
                canonical_slug,
                ProjectConfig::normalize_product_selector(canonical_slug),
                canonical_slug,
            };
        } else if (project_config->products.size() > 1) {
            throw ConfigError("Multiple products found; specify product explicitly.");
        } else {
            throw ConfigError("No products defined in project config");
        }
    } else {
        const auto resolution = project_config->resolve_product(*product_name_opt);
        if (!resolution) {
            throw ConfigError("Product '" + *product_name_opt + "' not found in project config");
        }
        product_resolution = *resolution;
    }

    const auto& product_name = product_resolution.canonical_slug;
    auto product_root = project_config->resolve_backlog_root(product_resolution, *config_path);
    if (!product_root) {
        throw ConfigError("Product '" + product_name + "' not found in project config");
    }

    std::filesystem::path project_root = ConfigLoader::resolve_project_root(*config_path).value_or(infer_project_root(*config_path));

    std::filesystem::path backlog_root = *product_root;
    if (product_root->parent_path().filename() == "products") {
        backlog_root = product_root->parent_path().parent_path();
    }

    BacklogContext ctx;
    ctx.project_root = project_root;
    ctx.product_root = *product_root;
    ctx.backlog_root = backlog_root;
    ctx.product_name = product_name;
    ctx.product_resolution = product_resolution;
    
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
