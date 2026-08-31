#include "kano/backlog_ops/config/config_ops.hpp"
#include <json/json.h>
#include <sstream>

namespace kano::backlog_ops {

namespace {

Json::Value StringArrayJson(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) {
        result.append(value);
    }
    return result;
}

Json::Value OptionalTextJson(const std::optional<std::string>& value) {
    return value ? Json::Value(*value) : Json::Value(Json::nullValue);
}

std::string SelectorListSummary(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << values[index];
    }
    out << ']';
    return out.str();
}

}

std::string ConfigOps::dump_effective_config_json(const kano::backlog_core::BacklogContext& ctx) {
    Json::Value root(Json::objectValue);

    Json::Value context(Json::objectValue);
    context["project_root"] = ctx.project_root.string();
    context["backlog_root"] = ctx.backlog_root.string();
    context["product_root"] = ctx.product_root.string();
    context["product_name"] = ctx.product_name;
    context["requested_product"] = ctx.product_resolution.requested_selector;
    context["canonical_product"] = ctx.product_resolution.canonical_slug;
    context["resolution_kind"] = kano::backlog_core::to_string(
        ctx.product_resolution.resolution_kind);
    context["normalized_selector"] = ctx.product_resolution.normalized_selector;
    context["matched_selector"] = ctx.product_resolution.matched_selector;
    context["is_sandbox"] = ctx.is_sandbox;
    if (ctx.sandbox_root) {
        context["sandbox_root"] = ctx.sandbox_root->string();
    } else {
        context["sandbox_root"] = Json::Value(Json::nullValue);
    }
    root["context"] = context;

    Json::Value config(Json::objectValue);
    const auto& pd = ctx.product_def;
    Json::Value product(Json::objectValue);
    product["name"] = pd.name.empty() ? ctx.product_name : pd.name;
    product["prefix"] = pd.prefix;
    product["backlog_root"] = pd.backlog_root;
    product["aliases"] = StringArrayJson(pd.aliases);
    product["repo_bindings"] = StringArrayJson(pd.repo_bindings);
    product["default_assignee"] = OptionalTextJson(pd.default_assignee);
    product["default_bug_reviewer"] = OptionalTextJson(pd.default_bug_reviewer);
    product["topics_date_prefix_policy"] = pd.topics_date_prefix_policy.empty()
        ? "warn"
        : pd.topics_date_prefix_policy;
    config["product"] = product;

    Json::Value embedding(Json::objectValue);
    embedding["provider"] = pd.embedding_provider.value_or("noop");
    embedding["model"] = pd.embedding_model.value_or("noop-embedding");
    embedding["dimension"] = pd.embedding_dimension.value_or(1536);
    config["embedding"] = embedding;

    Json::Value chunking(Json::objectValue);
    chunking["target_tokens"] = pd.chunking_target_tokens.value_or(256);
    chunking["max_tokens"] = pd.chunking_max_tokens.value_or(512);
    config["chunking"] = chunking;

    Json::Value tokenizer(Json::objectValue);
    tokenizer["adapter"] = pd.tokenizer_adapter.value_or("auto");
    tokenizer["model"] = pd.tokenizer_model.value_or("text-embedding-3-small");
    config["tokenizer"] = tokenizer;

    Json::Value vector(Json::objectValue);
    vector["enabled"] = pd.vector_enabled.value_or(false);
    vector["backend"] = pd.vector_backend.value_or("sqlite");
    vector["metric"] = pd.vector_metric.value_or("cosine");
    config["vector"] = vector;

    root["config"] = config;

    Json::StreamWriterBuilder wbuilder;
    wbuilder["indentation"] = "  ";
    return Json::writeString(wbuilder, root);
}

std::string ConfigOps::get_config_summary(const kano::backlog_core::BacklogContext& ctx) {
    std::stringstream ss;
    ss << "Product: " << ctx.product_name << "\n";
    ss << "Requested product: " << ctx.product_resolution.requested_selector << "\n";
    ss << "Canonical product: " << ctx.product_resolution.canonical_slug << "\n";
    ss << "Resolution kind: " << kano::backlog_core::to_string(
        ctx.product_resolution.resolution_kind) << "\n";
    ss << "Normalized selector: " << ctx.product_resolution.normalized_selector << "\n";
    ss << "Matched selector: " << ctx.product_resolution.matched_selector << "\n";
    ss << "Aliases: " << SelectorListSummary(ctx.product_def.aliases) << "\n";
    ss << "Repo bindings: " << SelectorListSummary(ctx.product_def.repo_bindings) << "\n";
    ss << "Prefix:  " << ctx.product_def.prefix << "\n";
    ss << "Root:    " << ctx.product_root.string() << "\n";
    return ss.str();
}

} // namespace kano::backlog_ops
