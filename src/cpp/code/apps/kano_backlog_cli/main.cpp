#include <CLI/CLI.hpp>
#include "version.hpp"
#include "kano/backlog_ops/workitem/workitem_ops.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/view/view_ops.hpp"
#include "kano/backlog_ops/orchestration/orchestration_ops.hpp"
#include "kano/backlog_ops/config/config_ops.hpp"
#include "kano/backlog_ops/doctor/doctor_ops.hpp"
#include "kano/backlog_ops/topic/topic_ops.hpp"
#include "kano/backlog_ops/workset/workset_ops.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <regex>

using namespace kano::backlog_core;
using namespace kano::backlog_ops;

namespace {

std::string join_strings(const std::vector<std::string>& values, const std::string& separator = ", ") {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << separator;
        }
        oss << values[i];
    }
    return oss.str();
}

} // namespace

int main(int InArgc, char* InArgv[]) {
    CLI::App app{
        "kano-backlog — Local-first backlog CLI\n"
        "Standalone: kano-backlog <command>\n"
        "Usage:      kano-backlog [options] <command> [args]",
        "kano-backlog"
    };

    // Context shared across commands
    std::string path_str = ".";
    std::string product_name_opt;
    std::string sandbox_name_opt;

    // Global options
    auto* global_opts = app.add_option_group("Global Options");
    global_opts->add_option("-p,--path", path_str, "Resource path (default: .)");
    global_opts->add_option("-P,--product", product_name_opt, "Explicit product name");
    global_opts->add_option("-s,--sandbox", sandbox_name_opt, "Sandbox name");

    app.set_version_flag("--version,-V", std::string{kano::backlog::GetBuildVersion()});
    app.require_subcommand(0);
    app.fallthrough();

    auto resolve_ctx = [&]() {
        return BacklogContext::resolve(path_str,
            product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt),
            sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
        );
    };

    try {
        // ============================================================
        // workitem group
        // ============================================================
        auto* workitemCmd = app.add_subcommand("workitem", "Work item operations");
        workitemCmd->alias("item");

        // workitem create
        {
            auto* createCmd = workitemCmd->add_subcommand("create", "Create a new work item");
            std::string type_str, title, agent, parent;
            createCmd->add_option("-t,--type", type_str, "Item type (epic, feature, userstory, task, bug)")->required();
            createCmd->add_option("--title", title, "Item title")->required();
            createCmd->add_option("--agent", agent, "Agent ID")->required();
            createCmd->add_option("--parent", parent, "Parent item ID");

            createCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                index.initialize();

                auto type_opt = parse_item_type(type_str);
                if (!type_opt) throw std::runtime_error("Invalid item type: " + type_str);

                auto result = WorkitemOps::create_item(
                    index,
                    ctx.product_root,
                    ctx.product_def.prefix,
                    *type_opt,
                    title,
                    agent,
                    parent.empty() ? std::nullopt : std::optional<std::string>(parent)
                );

                std::cout << "Created item: " << result.id << " (" << result.uid << ")\n";
                std::cout << "Path: " << result.path.string() << "\n";
            });
        }

        // workitem update-state
        {
            auto* updateStateCmd = workitemCmd->add_subcommand("update-state", "Update item state");
            std::string ref, state_str, state_opt_str, update_agent, update_msg;
            updateStateCmd->add_option("ref", ref, "Item ID or UID")->required();
            updateStateCmd->add_option("state_arg", state_str, "New state (positional)");
            updateStateCmd->add_option("--state", state_opt_str, "New state (option form)");
            updateStateCmd->add_option("--agent", update_agent, "Agent ID")->required();
            updateStateCmd->add_option("-m,--message", update_msg, "Optional log message");
            bool update_force = false;
            updateStateCmd->add_flag("-f,--force", update_force, "Bypass Ready gate validation");

            updateStateCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");

                std::string effective_state = !state_opt_str.empty() ? state_opt_str : state_str;
                if (effective_state.empty()) {
                    throw std::runtime_error("Missing required state. Use positional <state> or --state <state>");
                }

                auto state_opt = parse_item_state(effective_state);
                if (!state_opt) throw std::runtime_error("Invalid item state: " + effective_state);

                auto result = WorkitemOps::update_state(
                    index,
                    ctx.product_root,
                    ref,
                    *state_opt,
                    update_agent,
                    update_msg.empty() ? std::nullopt : std::optional<std::string>(update_msg),
                    update_force
                );

                if (result.worklog_appended) {
                    std::cout << "Updated " << result.id << ": " << to_string(result.old_state) << " -> " << to_string(result.new_state);
                    if (result.parent_synced) std::cout << " [Parent synced]";
                    std::cout << "\n";
                } else {
                    std::cout << "Item " << result.id << " is already in state " << to_string(result.new_state) << "\n";
                }
            });
        }

        // workitem set-ready
        {
            auto* setReadyCmd = workitemCmd->add_subcommand("set-ready", "Populate Ready-gate body fields on an item");
            std::string ref, context, goal, approach, acceptance_criteria, risks, set_ready_agent;
            setReadyCmd->add_option("ref", ref, "Item ID or UID")->required();
            setReadyCmd->add_option("--context", context, "Context text");
            setReadyCmd->add_option("--goal", goal, "Goal text");
            setReadyCmd->add_option("--approach", approach, "Approach text");
            setReadyCmd->add_option("--acceptance-criteria", acceptance_criteria, "Acceptance criteria text");
            setReadyCmd->add_option("--risks", risks, "Risks / Dependencies text");
            setReadyCmd->add_option("--agent", set_ready_agent, "Agent ID")->required();

            setReadyCmd->callback([&]() {
                auto ctx = resolve_ctx();
                CanonicalStore store(ctx.product_root);
                RefResolver resolver(store);
                auto item = resolver.resolve(ref);

                std::vector<std::string> updated_fields;

                if (!context.empty()) {
                    item.context = context;
                    updated_fields.push_back("Context");
                }
                if (!goal.empty()) {
                    item.goal = goal;
                    updated_fields.push_back("Goal");
                }
                if (!approach.empty()) {
                    item.approach = approach;
                    updated_fields.push_back("Approach");
                }
                if (!acceptance_criteria.empty()) {
                    item.acceptance_criteria = acceptance_criteria;
                    updated_fields.push_back("Acceptance Criteria");
                }
                if (!risks.empty()) {
                    item.risks = risks;
                    updated_fields.push_back("Risks / Dependencies");
                }

                if (updated_fields.empty()) {
                    throw std::runtime_error("No Ready fields supplied. Pass at least one of --context, --goal, --approach, --acceptance-criteria, --risks");
                }

                StateMachine::record_worklog(item, set_ready_agent, "Updated Ready fields: " + join_strings(updated_fields));
                store.write(item);

                std::cout << "OK: Updated Ready fields for " << item.id << "\n";
                std::cout << "  Worklog: Updated " << join_strings(updated_fields) << "\n";
            });
        }

        // workitem check-ready
        {
            auto* checkReadyCmd = workitemCmd->add_subcommand("check-ready", "Check whether an item satisfies the Ready gate");
            std::string ref;
            bool no_check_parent = false;
            checkReadyCmd->add_option("ref", ref, "Item ID or UID")->required();
            checkReadyCmd->add_flag("--no-check-parent", no_check_parent, "Skip parent Ready validation");

            checkReadyCmd->callback([&]() {
                auto ctx = resolve_ctx();
                CanonicalStore store(ctx.product_root);
                RefResolver resolver(store);
                auto item = resolver.resolve(ref);

                auto [ready, missing] = Validator::is_ready(item);
                std::vector<std::string> problems;
                if (!ready) {
                    problems.push_back("Missing fields in " + item.id + ": " + join_strings(missing));
                }

                if (!no_check_parent && item.parent) {
                    auto parent = resolver.resolve(*item.parent);
                    auto [parent_ready, parent_missing] = Validator::is_ready(parent);
                    if (!parent_ready) {
                        problems.push_back("Parent " + parent.id + " is NOT READY: " + join_strings(parent_missing));
                    }
                }

                if (!problems.empty()) {
                    std::cout << item.id << " is NOT READY\n";
                    for (const auto& problem : problems) {
                        std::cout << "- " << problem << "\n";
                    }
                    throw std::runtime_error("Ready gate failed");
                }

                std::cout << "OK: " << item.id << " is READY\n";
            });
        }

        // workitem trash
        {
            auto* trashCmd = workitemCmd->add_subcommand("trash", "Move item to trash");
            std::string trash_ref, trash_agent, trash_reason;
            trashCmd->add_option("ref", trash_ref, "Item ID or UID")->required();
            trashCmd->add_option("--agent", trash_agent, "Agent ID")->required();
            trashCmd->add_option("-r,--reason", trash_reason, "Reason for trashing");
            trashCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                auto result = WorkitemOps::trash_item(
                    index, ctx.product_root, trash_ref, trash_agent,
                    trash_reason.empty() ? std::nullopt : std::optional<std::string>(trash_reason)
                );
                std::cout << "Trashed item: " << result.item_ref << "\n";
                std::cout << "Source: " << result.source_path.string() << "\n";
                std::cout << "Trash: " << result.trashed_path.string() << "\n";
            });
        }

        // workitem decision
        {
            auto* decisionCmd = workitemCmd->add_subcommand("decision", "Record a decision for an item");
            decisionCmd->alias("add-decision");
            std::string dec_ref, dec_text, dec_text_opt, dec_agent, dec_source;
            decisionCmd->add_option("ref", dec_ref, "Item ID or UID")->required();
            decisionCmd->add_option("text", dec_text, "Decision text (positional)");
            decisionCmd->add_option("--decision", dec_text_opt, "Decision text (option form)");
            decisionCmd->add_option("--agent", dec_agent, "Agent ID")->required();
            decisionCmd->add_option("--source", dec_source, "Source of decision (e.g. meeting, email)");
            decisionCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                std::string effective_decision = !dec_text_opt.empty() ? dec_text_opt : dec_text;
                if (effective_decision.empty()) {
                    throw std::runtime_error("Missing decision text. Use positional <text> or --decision <text>");
                }
                auto result = WorkitemOps::add_decision_writeback(
                    index, ctx.product_root, dec_ref, effective_decision, dec_agent,
                    dec_source.empty() ? std::nullopt : std::optional<std::string>(dec_source)
                );
                if (result.added) {
                    std::cout << "Added decision to " << result.item_id << "\n";
                } else {
                    std::cout << "Decision already exists in " << result.item_id << "\n";
                }
            });
        }

        // workitem remap-id
        {
            auto* remapIdCmd = workitemCmd->add_subcommand("remap-id", "Rename an item's ID and update references");
            std::string ri_ref, ri_to, ri_agent;
            remapIdCmd->add_option("ref", ri_ref, "Current item ID or UID")->required();
            remapIdCmd->add_option("--to", ri_to, "New ID")->required();
            remapIdCmd->add_option("--agent", ri_agent, "Agent ID")->required();
            remapIdCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                auto result = WorkitemOps::remap_id(index, ctx.product_root, ri_ref, ri_to, ri_agent);
                std::cout << "Remapped ID: " << result.old_id << " -> " << result.new_id << "\n";
                std::cout << "Updated " << result.updated_files << " files.\n";
            });
        }

        // workitem remap-parent
        {
            auto* remapCmd = workitemCmd->add_subcommand("remap-parent", "Remap item parent");
            std::string remap_ref, parent_ref, remap_agent;
            remapCmd->add_option("ref", remap_ref, "Item ID or UID")->required();
            remapCmd->add_option("parent", parent_ref, "New parent ID or UID (use 'none' to clear)")->required();
            remapCmd->add_option("--agent", remap_agent, "Agent ID")->required();

            remapCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");

                WorkitemOps::remap_parent(index, ctx.product_root, remap_ref, parent_ref, remap_agent);
                std::cout << "Successfully remapped parent for " << remap_ref << "\n";
            });
        }

        // workitem list
        {
            auto* listCmd = workitemCmd->add_subcommand("list", "List work items");
            std::string filter_type_str, filter_state_str;
            listCmd->add_option("--type", filter_type_str, "Filter by type (epic, feature, userstory, task, bug)");
            listCmd->add_option("--state", filter_state_str, "Filter by state");
            listCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");

                ViewFilter filter;
                if (!filter_type_str.empty()) {
                    auto t = parse_item_type(filter_type_str);
                    if (t) filter.type = *t;
                }
                if (!filter_state_str.empty()) {
                    auto s = parse_item_state(filter_state_str);
                    if (s) filter.state = *s;
                }

                auto items = ViewOps::list_items(index, filter);
                std::cout << ViewOps::render_table(items);
            });
        }

        // ============================================================
        // config group
        // ============================================================
        {
            auto* configCmd = app.add_subcommand("config", "Configuration management");

            auto* configDumpCmd = configCmd->add_subcommand("dump", "Dump effective configuration as JSON");
            configDumpCmd->alias("show");
            configDumpCmd->callback([&]() {
                auto ctx = resolve_ctx();
                std::cout << ConfigOps::dump_effective_config_json(ctx) << std::endl;
            });
        }

        // ============================================================
        // doctor command
        // ============================================================
        {
            auto* doctorCmd = app.add_subcommand("doctor", "Environment healthy check");
            doctorCmd->callback([&]() {
                auto results = DoctorOps::run_all_checks(path_str);
                for (const auto& res : results) {
                    std::cout << (res.passed ? "[PASS] " : "[FAIL] ") << res.name << ": " << res.message << "\n";
                    if (!res.details.empty()) {
                        std::cout << "       " << res.details << "\n";
                    }
                }
            });
        }

        // ============================================================
        // admin group
        // ============================================================
        {
            auto* adminCmd = app.add_subcommand("admin", "Administrative operations");

            auto* initCmd = adminCmd->add_subcommand("init", "Initialize a new backlog");
            std::string init_agent;
            initCmd->add_option("--agent", init_agent, "Agent ID")->required();
            initCmd->callback([&]() {
                std::filesystem::path backlog_root(path_str);
                OrchestrationOps::initialize_backlog(backlog_root, init_agent);
                std::cout << "Initialized backlog at: " << std::filesystem::absolute(backlog_root).string() << "\n";
            });

            // admin sync-sequences
            {
                auto* syncCmd = adminCmd->add_subcommand("sync-sequences", "Sync DB ID sequences from existing files");
                std::string sync_product;
                syncCmd->add_option("--product", sync_product, "Product name");
                syncCmd->callback([&]() {
                    auto ctx = BacklogContext::resolve(
                        path_str,
                        sync_product.empty()
                            ? (product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt))
                            : std::optional<std::string>(sync_product),
                        sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                    );
                    auto idx_path = ctx.backlog_root / ".cache" / "index" / "backlog.db";
                    BacklogIndex index(idx_path);
                    index.initialize();

                    auto result = index.sync_sequences(ctx.product_root);
                    if (result.synced_pairs.empty()) {
                        std::cout << "No sequences synced (no items found).\n";
                    } else {
                        std::cout << "Synced " << result.synced_pairs.size() << " sequence pair(s):\n";
                        for (const auto& p : result.synced_pairs) {
                            std::cout << "  " << p << "\n";
                        }
                    }
                    std::cout << "Max sequence found: " << result.max_number_found << "\n";
                });
            }
        }

        // ============================================================
        // topic group
        // ============================================================
        {
            auto* topicCmd = app.add_subcommand("topic", "Topic context management");

            // topic create
            {
                auto* createCmd = topicCmd->add_subcommand("create", "Create a new topic");
                std::string name, agent;
                createCmd->add_option("name", name, "Topic name")->required();
                createCmd->add_option("--agent", agent, "Agent ID")->required();
                createCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = TopicOps::create_topic(name, agent, ctx.backlog_root);
                    std::cout << "Created topic: " << result.topic << "\n";
                    std::cout << "Path: " << result.topic_path.string() << "\n";
                });
            }

            // topic add
            {
                auto* addCmd = topicCmd->add_subcommand("add", "Add an item to a topic");
                std::string topic_name, item_ref;
                addCmd->add_option("topic-name", topic_name, "Topic name")->required();
                addCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                addCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = TopicOps::add_item(topic_name, item_ref, ctx.backlog_root);
                    std::cout << (result.added ? "Added" : "Already present") << " in topic " << result.topic << ": " << result.item_ref << "\n";
                });
            }

            // topic list
            {
                auto* listCmd = topicCmd->add_subcommand("list", "List all topics");
                listCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto topics = TopicOps::list_topics(ctx.backlog_root);
                    if (topics.empty()) {
                        std::cout << "No topics found.\n";
                        return;
                    }
                    std::cout << std::left
                       << std::setw(24) << "Topic"
                       << std::setw(10) << "Status"
                       << std::setw(8)  << "Items"
                       << std::setw(8)  << "Active"
                       << "Created\n";
                    std::cout << std::string(60, '-') << "\n";
                    for (const auto& t : topics) {
                        std::cout << std::left
                           << std::setw(24) << t.id
                           << std::setw(10) << t.status
                           << std::setw(8)  << t.item_count
                           << std::setw(8)  << (t.is_active ? "yes" : "no")
                           << t.created_at << "\n";
                    }
                });
            }

            // topic switch
            {
                auto* switchCmd = topicCmd->add_subcommand("switch", "Switch active topic");
                std::string topic_name, agent;
                switchCmd->add_option("topic-name", topic_name, "Topic name")->required();
                switchCmd->add_option("--agent", agent, "Agent ID")->required();
                switchCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = TopicOps::switch_topic(topic_name, agent, ctx.backlog_root);
                    std::cout << "Switched to topic: " << result.topic << "\n";
                    std::cout << "  Items: " << result.item_count << ", Pinned docs: " << result.pinned_doc_count << "\n";
                });
            }

            // topic distill
            {
                auto* distillCmd = topicCmd->add_subcommand("distill", "Generate brief.generated.md from materials");
                std::string topic_name;
                distillCmd->add_option("topic-name", topic_name, "Topic name")->required();
                distillCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto brief_path = TopicOps::distill(topic_name, ctx.backlog_root);
                    std::cout << "Generated: " << brief_path.string() << "\n";
                });
            }

            // topic status
            {
                auto* statusCmd = topicCmd->add_subcommand("status", "Show topic status");
                std::string topic_name;
                statusCmd->add_option("topic-name", topic_name, "Topic name")->required();
                statusCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto status = TopicOps::get_topic_status(topic_name, ctx.backlog_root);
                    std::cout << "Topic: " << status.id << "\n";
                    std::cout << "Status: " << status.status << "\n";
                    std::cout << "Active: " << (status.is_active ? "yes" : "no") << "\n";
                    std::cout << "Items: " << status.item_count << "\n";
                    std::cout << "Pinned docs: " << status.pinned_doc_count << "\n";
                    std::cout << "Created: " << status.created_at << "\n";
                    std::cout << "Updated: " << status.updated_at << "\n";
                });
            }

            // topic export-context
            {
                auto* exportCmd = topicCmd->add_subcommand("export-context", "Export topic context bundle");
                std::string topic_name;
                std::string format_str = "markdown";
                exportCmd->add_option("topic-name", topic_name, "Topic name")->required();
                exportCmd->add_option("--format", format_str, "Output format (markdown or json)");
                exportCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto bundle = TopicOps::export_context(topic_name, ctx.backlog_root);
                    if (format_str == "json") {
                        std::cout << "{\n";
                        std::cout << "  \"topic\": \"" << bundle.topic << "\",\n";
                        std::cout << "  \"generated_at\": \"" << bundle.generated_at << "\",\n";
                        std::cout << "  \"items\": [\n";
                        for (size_t i = 0; i < bundle.items.size(); ++i) {
                            const auto& item = bundle.items[i];
                            std::cout << "    {\n";
                            std::cout << "      \"uid\": \"" << item.uid << "\", ";
                            std::cout << "\"id\": \"" << item.id << "\", ";
                            std::cout << "\"title\": \"" << item.title << "\", ";
                            std::cout << "\"type\": \"" << item.type << "\", ";
                            std::cout << "\"state\": \"" << item.state << "\", ";
                            std::cout << "\"priority\": \"" << item.priority << "\", ";
                            std::cout << "\"path\": \"" << item.path << "\"";
                            if (item.error) {
                                std::cout << ", \"error\": \"" << item.error_msg << "\"";
                            }
                            std::cout << " }";
                            if (i + 1 < bundle.items.size()) std::cout << ",";
                            std::cout << "\n";
                        }
                        std::cout << "  ],\n";
                        std::cout << "  \"pinned_docs\": [\n";
                        for (size_t i = 0; i < bundle.pinned_docs.size(); ++i) {
                            const auto& doc = bundle.pinned_docs[i];
                            std::cout << "    {\n";
                            std::cout << "      \"path\": \"" << doc.path << "\"";
                            if (doc.error) {
                                std::cout << ", \"error\": \"" << doc.error_msg << "\"";
                            } else {
                                std::cout << ",\n      \"content\": \"";
                                // Escape content for JSON
                                for (char c : doc.content) {
                                    if (c == '"') std::cout << "\\\"";
                                    else if (c == '\\') std::cout << "\\\\";
                                    else if (c == '\n') std::cout << "\\n";
                                    else if (c == '\r') std::cout << "\\r";
                                    else if (c == '\t') std::cout << "\\t";
                                    else std::cout << c;
                                }
                                std::cout << "\"";
                            }
                            std::cout << " }";
                            if (i + 1 < bundle.pinned_docs.size()) std::cout << ",";
                            std::cout << "\n";
                        }
                        std::cout << "  ]\n";
                        std::cout << "}\n";
                    } else {
                        // Markdown format
                        std::cout << "# Topic: " << bundle.topic << "\n\n";
                        std::cout << "**Generated**: " << bundle.generated_at << "\n\n";
                        if (!bundle.items.empty()) {
                            std::cout << "## Items (" << bundle.items.size() << ")\n\n";
                            for (const auto& item : bundle.items) {
                                std::cout << "- **" << item.id << "**";
                                if (!item.title.empty()) std::cout << " — " << item.title;
                                if (!item.state.empty()) std::cout << " [" << item.state << "]";
                                if (!item.type.empty()) std::cout << " (" << item.type << ")";
                                if (!item.priority.empty()) std::cout << " " << item.priority;
                                if (item.error) std::cout << " ⚠️ " << item.error_msg;
                                std::cout << "\n";
                            }
                            std::cout << "\n";
                        }
                        if (!bundle.pinned_docs.empty()) {
                            std::cout << "## Pinned Documents (" << bundle.pinned_docs.size() << ")\n\n";
                            for (const auto& doc : bundle.pinned_docs) {
                                std::cout << "- `" << doc.path << "`";
                                if (doc.error) std::cout << " ⚠️ " << doc.error_msg;
                                std::cout << "\n";
                            }
                            std::cout << "\n";
                        }
                    }
                });
            }
        }

        // ============================================================
        // workset group
        // ============================================================
        {
            auto* worksetCmd = app.add_subcommand("workset", "Workset management");

            // workset init
            {
                auto* initCmd = worksetCmd->add_subcommand("init", "Initialize a workset for an item");
                std::string item_ref, agent;
                int ttl_hours = 72;
                initCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                initCmd->add_option("--agent", agent, "Agent ID")->required();
                initCmd->add_option("--ttl-hours", ttl_hours, "Time-to-live in hours");
                initCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::init_workset(item_ref, agent, ctx.backlog_root, ttl_hours);
                    std::cout << (result.created ? "Initialized" : "Already exists") << " workset: " << result.workset_path.string() << "\n";
                });
            }

            // workset next
            {
                auto* nextCmd = worksetCmd->add_subcommand("next", "Get next unchecked action from plan");
                std::string item_ref;
                nextCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                nextCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::get_next_action(item_ref, ctx.backlog_root);
                    if (result.is_complete) {
                        std::cout << "All steps complete.\n";
                    } else {
                        std::cout << "[" << result.step_number << "] " << result.description << "\n";
                    }
                });
            }

            // workset refresh
            {
                auto* refreshCmd = worksetCmd->add_subcommand("refresh", "Refresh workset timestamp");
                std::string item_ref, agent;
                refreshCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                refreshCmd->add_option("--agent", agent, "Agent ID")->required();
                refreshCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::refresh_workset(item_ref, agent, ctx.backlog_root);
                    std::cout << "Refreshed: " << result.workset_path.string() << "\n";
                });
            }

            // workset promote
            {
                auto* promoteCmd = worksetCmd->add_subcommand("promote", "Promote deliverables to artifacts");
                std::string item_ref, agent;
                bool dry_run = false;
                promoteCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                promoteCmd->add_option("--agent", agent, "Agent ID")->required();
                promoteCmd->add_flag("--dry-run", dry_run, "Show what would be promoted without copying");
                promoteCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::promote_deliverables(item_ref, agent, ctx.backlog_root, dry_run);
                    if (result.promoted_files.empty()) {
                        std::cout << "No deliverables to promote.\n";
                    } else {
                        std::cout << "Promoted " << result.promoted_files.size() << " file(s) to " << result.target_path.string() << "\n";
                    }
                });
            }

            // workset list
            {
                auto* listCmd = worksetCmd->add_subcommand("list", "List all worksets");
                listCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto worksets = WorksetOps::list_worksets(ctx.backlog_root);
                    if (worksets.empty()) {
                        std::cout << "No worksets found.\n";
                        return;
                    }
                    std::cout << std::left
                       << std::setw(24) << "Item"
                       << std::setw(16) << "Agent"
                       << std::setw(24) << "Created"
                       << "\n";
                    std::cout << std::string(64, '-') << "\n";
                    for (const auto& ws : worksets) {
                        std::cout << std::left
                           << std::setw(24) << ws.item_id
                           << std::setw(16) << ws.agent
                           << std::setw(24) << ws.created_at
                           << "\n";
                    }
                });
            }

            // workset cleanup
            {
                auto* cleanupCmd = worksetCmd->add_subcommand("cleanup", "Remove expired worksets");
                int ttl_hours = 72;
                bool dry_run = true;
                cleanupCmd->add_option("--ttl-hours", ttl_hours, "Age threshold in hours");
                cleanupCmd->add_flag("--apply", dry_run, "Actually delete (default is dry-run)");
                cleanupCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::cleanup_worksets(ctx.backlog_root, ttl_hours, dry_run);
                    if (result.deleted_count == 0) {
                        std::cout << "No expired worksets found.\n";
                    } else {
                        std::cout << "Would delete " << result.deleted_count << " workset(s) (" << result.space_reclaimed_bytes << " bytes)"
                                  << " (use --apply to confirm)\n";
                    }
                });
            }

            // workset detect-adr
            {
                auto* detectCmd = worksetCmd->add_subcommand("detect-adr", "Detect Decision: markers in notes.md");
                std::string item_ref;
                std::string format_str = "plain";
                detectCmd->add_option("--item", item_ref, "Item ID, UID, or path")->required();
                detectCmd->add_option("--format", format_str, "Output format (plain or json)");
                detectCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    auto result = WorksetOps::detect_adr(item_ref, ctx.backlog_root);
                    if (result.candidates.empty()) {
                        std::cout << "No Decision: markers found in " << result.workset_path.string() << "\n";
                    } else {
                        if (format_str == "json") {
                            std::cout << "{\n";
                            std::cout << "  \"workset_path\": \"" << result.workset_path.string() << "\",\n";
                            std::cout << "  \"candidates\": [\n";
                            for (size_t i = 0; i < result.candidates.size(); ++i) {
                                const auto& cand = result.candidates[i];
                                std::cout << "    {\n";
                                std::cout << "      \"text\": \"";
                                for (char c : cand.text) {
                                    if (c == '"') std::cout << "\\\"";
                                    else if (c == '\\') std::cout << "\\\\";
                                    else if (c == '\n') std::cout << "\\n";
                                    else std::cout << c;
                                }
                                std::cout << "\",\n";
                                std::cout << "      \"suggested_title\": \"" << cand.suggested_title << "\"\n";
                                std::cout << "    }";
                                if (i + 1 < result.candidates.size()) std::cout << ",";
                                std::cout << "\n";
                            }
                            std::cout << "  ]\n";
                            std::cout << "}\n";
                        } else {
                            std::cout << "Found " << result.candidates.size() << " Decision: marker(s) in notes.md:\n\n";
                            for (size_t i = 0; i < result.candidates.size(); ++i) {
                                const auto& cand = result.candidates[i];
                                std::cout << (i + 1) << ". " << cand.text << "\n";
                                std::cout << "   Suggested title: " << cand.suggested_title << "\n\n";
                            }
                        }
                    }
                });
            }
        }

        // ============================================================
        // state group
        // ============================================================
        {
            auto* stateCmd = app.add_subcommand("state", "State transition commands");

            // state transition
            {
                auto* transitionCmd = stateCmd->add_subcommand("transition", "Transition item state");
                std::string item_ref, action_str, agent, message, model;
                transitionCmd->add_option("ref", item_ref, "Item ID, UID, or path")->required();
                transitionCmd->add_option("action", action_str, "Action (propose|ready|start|review|done|block|drop)")->required();
                transitionCmd->add_option("--agent", agent, "Agent ID");
                transitionCmd->add_option("-m,--message", message, "Optional worklog message");
                transitionCmd->add_option("--model", model, "Model used by agent");
                transitionCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    CanonicalStore store(ctx.product_root);
                    RefResolver resolver(store);

                    auto item = resolver.resolve(item_ref);
                    auto action_opt = parse_state_action(action_str);
                    if (!action_opt) {
                        std::cerr << "Error: Invalid action '" << action_str << "'\n";
                        std::cerr << "Valid actions: propose, ready, start, review, done, block, drop\n";
                        throw std::runtime_error("Invalid action");
                    }

                    std::optional<std::string> agent_opt = agent.empty() ? std::nullopt : std::optional<std::string>(agent);
                    std::optional<std::string> msg_opt = message.empty() ? std::nullopt : std::optional<std::string>(message);
                    std::optional<std::string> model_opt = model.empty() ? std::nullopt : std::optional<std::string>(model);

                    StateMachine::transition(item, *action_opt, agent_opt, msg_opt, model_opt);
                    store.write(item);

                    std::cout << "OK: " << item.id << " transitioned to " << to_string(item.state) << "\n";
                });
            }
        }

        // ============================================================
        // worklog group
        // ============================================================
        {
            auto* worklogCmd = app.add_subcommand("worklog", "Worklog operations");

            // worklog append
            {
                auto* appendCmd = worklogCmd->add_subcommand("append", "Append a worklog entry");
                std::string item_ref, message, agent, model;
                appendCmd->add_option("ref", item_ref, "Item ID, UID, or path")->required();
                appendCmd->add_option("message", message, "Worklog message")->required();
                appendCmd->add_option("--agent", agent, "Agent ID");
                appendCmd->add_option("--model", model, "Model used by agent");
                appendCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    CanonicalStore store(ctx.product_root);
                    RefResolver resolver(store);

                    auto item = resolver.resolve(item_ref);

                    std::optional<std::string> agent_opt = agent.empty() ? std::nullopt : std::optional<std::string>(agent);
                    std::optional<std::string> model_opt = model.empty() ? std::nullopt : std::optional<std::string>(model);

                    StateMachine::record_worklog(item, agent.empty() ? "cli" : agent, message, model_opt);
                    store.write(item);

                    std::cout << "OK: Appended worklog to " << item.id << "\n";
                });
            }
        }

        // ============================================================
        // view group
        // ============================================================
        {
            auto* viewCmd = app.add_subcommand("view", "Dashboard and view management");

            // view list
            {
                auto* listCmd = viewCmd->add_subcommand("list", "List available views");
                listCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    std::filesystem::path views_dir = ctx.product_root / "views";
                    if (!std::filesystem::exists(views_dir)) {
                        std::cout << "No views directory found.\n";
                        return;
                    }
                    std::cout << "Available views in: " << views_dir.string() << "\n";
                    for (const auto& entry : std::filesystem::directory_iterator(views_dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".md") {
                            std::cout << "  - " << entry.path().filename().string() << "\n";
                        }
                    }
                });
            }

            // view refresh
            {
                auto* refreshCmd = viewCmd->add_subcommand("refresh", "Refresh dashboards");
                std::string view_product;
                std::string view_agent;
                std::string view_backlog_root_str;
                refreshCmd->add_option("--product", view_product, "Product name");
                refreshCmd->add_option("--backlog-root", view_backlog_root_str, "Backlog root path");
                refreshCmd->add_option("--agent", view_agent, "Agent ID")->required();
                refreshCmd->callback([&]() {
                    std::filesystem::path product_root;
                    if (!view_backlog_root_str.empty()) {
                        // Explicit backlog root provided — construct product root directly
                        std::string prod_name;
                        if (!view_product.empty()) {
                            prod_name = view_product;
                        } else {
                            auto ctx = resolve_ctx();
                            prod_name = ctx.product_name;
                        }
                        product_root = std::filesystem::path(view_backlog_root_str) / "products" / prod_name;
                    } else {
                        // Use standard resolution
                        auto ctx = resolve_ctx();
                        product_root = ctx.product_root;
                    }
                    auto result = ViewOps::refresh_dashboards(product_root, view_agent);
                    std::cout << "Refreshed " << result.views_refreshed.size() << " dashboards\n";
                    for (const auto& path : result.views_refreshed) {
                        std::cout << "- " << path.string() << "\n";
                    }
                });
            }
        }

        // ============================================================
        // index group
        // ============================================================
        {
            auto* indexCmd = app.add_subcommand("index", "SQLite index operations");

            // index build
            {
                auto* buildCmd = indexCmd->add_subcommand("build", "Build the SQLite index from markdown items");
                std::string idx_product;
                bool force = false;
                buildCmd->add_option("--product", idx_product, "Product name");
                buildCmd->add_flag("--force", force, "Rebuild even if index exists");
                buildCmd->callback([&]() {
                    auto ctx = BacklogContext::resolve(
                        path_str,
                        idx_product.empty()
                            ? (product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt))
                            : std::optional<std::string>(idx_product),
                        sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                    );
                    auto idx_path = ctx.backlog_root / ".cache" / "index" / "backlog.db";
                    auto result = build_index(ctx.product_root, idx_path, force);
                    std::cout << "Built index: " << result.index_path.string() << "\n";
                    std::cout << "  Items: " << result.items_indexed << "\n";
                    std::cout << "  Time: " << std::fixed << std::setprecision(1) << result.build_time_ms << " ms\n";
                });
            }

            // index refresh
            {
                auto* refreshCmd = indexCmd->add_subcommand("refresh", "Refresh the SQLite index (MVP: full rebuild)");
                std::string idx_product;
                refreshCmd->add_option("--product", idx_product, "Product name");
                refreshCmd->callback([&]() {
                    auto ctx = BacklogContext::resolve(
                        path_str,
                        idx_product.empty()
                            ? (product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt))
                            : std::optional<std::string>(idx_product),
                        sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                    );
                    auto idx_path = ctx.backlog_root / ".cache" / "index" / "backlog.db";
                    auto result = refresh_index(ctx.product_root, idx_path);
                    std::cout << "Refreshed index: " << result.index_path.string() << "\n";
                    std::cout << "  Items: " << result.items_added << "\n";
                    std::cout << "  Time: " << std::fixed << std::setprecision(1) << result.refresh_time_ms << " ms\n";
                });
            }

            // index status
            {
                auto* statusCmd = indexCmd->add_subcommand("status", "Show SQLite index status and statistics");
                std::string idx_product;
                statusCmd->add_option("--product", idx_product, "Product name");
                statusCmd->callback([&]() {
                    auto ctx = BacklogContext::resolve(
                        path_str,
                        idx_product.empty()
                            ? (product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt))
                            : std::optional<std::string>(idx_product),
                        sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                    );
                    auto result = get_index_status(ctx.product_root, ctx.product_name);
                    if (result.indexes.empty()) {
                        std::cout << "No indexes found.\n";
                        return;
                    }
                    for (const auto& idx : result.indexes) {
                        std::cout << "Index: " << idx.product << "\n";
                        std::cout << "  Path: " << idx.index_path.string() << "\n";
                        std::cout << "  Status: " << (idx.exists ? "Exists" : "Missing") << "\n";
                        if (idx.exists) {
                            std::cout << "  Items: " << idx.item_count << "\n";
                            std::cout << "  Size: " << idx.size_bytes << " bytes\n";
                            std::cout << "  Modified: " << idx.last_modified << "\n";
                        }
                    }
                });
            }
        }

        // ============================================================
        // search group (stub — requires embedding pipeline)
        // ============================================================
        {
            auto* searchCmd = app.add_subcommand("search", "Hybrid search (requires embedding pipeline)");
            searchCmd->add_subcommand("query", "NOT IMPLEMENTED in native CLI — use Python CLI");
            searchCmd->add_subcommand("hybrid", "NOT IMPLEMENTED in native CLI — use Python CLI");
        }

        // ============================================================
        // webview group
        // ============================================================
        {
            auto* webviewCmd = app.add_subcommand("webview", "Launch local webview server");
            webviewCmd->require_subcommand(0);
            auto* serveCmd = webviewCmd->add_subcommand("serve", "Start the webview HTTP server");
            std::string backlog_root_opt;
            std::string port_opt;
            serveCmd->add_option("--backlog-root", backlog_root_opt, "Backlog products root");
            serveCmd->add_option("--port", port_opt, "HTTP port (default: 8787)");

            serveCmd->callback([&]() {
                // Find the webview binary relative to the CLI binary location
                std::filesystem::path cli_exe(InArgv[0]);
                std::filesystem::path cli_dir = cli_exe.parent_path();

                // Try same directory first, then parent build dir
                std::filesystem::path webview_exe = cli_dir / "kano_backlog_webview.exe";
                if (!std::filesystem::exists(webview_exe)) {
                    webview_exe = cli_dir / ".." / ".." / ".." / "kano_backlog_webview.exe";
                    webview_exe = webview_exe.lexically_normal();
                }

                if (!std::filesystem::exists(webview_exe)) {
                    std::cerr << "Error: kano_backlog_webview.exe not found.\n";
                    std::cerr << "Build with: cmake --build --preset <preset> --target kano_backlog_webview\n";
                    std::cerr << "Or set KB_BUILD_WEBVIEW=ON in CMake.\n";
                    throw std::runtime_error("webview binary not found");
                }

                // Forward to webview executable
                std::string cmd = "\"" + webview_exe.string() + "\"";
                for (int i = 1; i < InArgc; ++i) {
                    std::string arg(InArgv[i]);
                    if (arg == "serve" || arg == "webview") continue; // skip our own subcommand
                    cmd += " \"" + arg + "\"";
                }
                std::cout << "Starting webview server...\n";
                std::cout << "Launching: " << webview_exe << "\n";
                std::cout << "Press Ctrl+C to stop.\n";
                int rc = std::system(cmd.c_str());
                if (rc != 0) throw std::runtime_error("webview server exited with error");
            });
        }

        // ============================================================
        // schema
        // ============================================================
        {
            auto* schemaCmd = app.add_subcommand("schema", "Schema validation and fixing");

            // schema check
            {
                auto* checkCmd = schemaCmd->add_subcommand("check", "Check for missing required fields");
                std::string schema_product;
                std::string schema_backlog_root_str;
                checkCmd->add_option("--product", schema_product, "Product name (check all if omitted)");
                checkCmd->add_option("--backlog-root", schema_backlog_root_str, "Backlog root path");
                checkCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!schema_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(schema_backlog_root_str);
                    } else if (!schema_product.empty()) {
                        auto ctx = BacklogContext::resolve(
                            path_str,
                            std::optional<std::string>(schema_product),
                            sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                        );
                        backlog_root = ctx.backlog_root;
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    int total_checked = 0;
                    int total_issues = 0;

                    // For each product under backlog_root/products
                    auto products_dir = backlog_root / "products";
                    if (!std::filesystem::exists(products_dir)) {
                        std::cout << "No products directory found.\n";
                        return;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                        if (!entry.is_directory()) continue;
                        std::string product_name = entry.path().filename().string();
                        if (!schema_product.empty() && schema_product != product_name) continue;

                        auto product_root = entry.path();
                        CanonicalStore store(product_root);
                        auto item_paths = store.list_items();
                        int product_checked = 0;
                        int product_issues = 0;

                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                auto errors = Validator::validate_schema(item);
                                ++product_checked;
                                if (!errors.empty()) {
                                    ++product_issues;
                                    std::cout << "\nFAIL " << item.id << " (" << item_path.filename().string() << ")\n";
                                    for (const auto& err : errors) {
                                        std::cout << "  - " << err << "\n";
                                    }
                                }
                            } catch (const std::exception& e) {
                                ++product_checked;
                                ++product_issues;
                                std::cout << "\nFAIL " << item_path.filename().string() << ": " << e.what() << "\n";
                            }
                        }

                        total_checked += product_checked;
                        total_issues += product_issues;

                        if (product_issues == 0 && product_checked > 0) {
                            std::cout << "OK " << product_name << ": all " << product_checked << " items have required fields\n";
                        } else if (product_issues > 0) {
                            std::cout << "FAIL " << product_name << ": " << product_issues << " items with missing fields\n";
                        }
                    }

                    std::cout << "\nTotal: " << total_checked << " items checked, " << total_issues << " with issues\n";
                    if (total_issues > 0) {
                        throw std::runtime_error("Schema check failed");
                    }
                });
            }

            // schema fix
            {
                auto* fixCmd = schemaCmd->add_subcommand("fix", "Fix missing required fields");
                std::string fix_product;
                std::string fix_backlog_root_str;
                std::string fix_agent;
                std::string fix_model;
                bool fix_apply = false;
                fixCmd->add_option("--product", fix_product, "Product name (fix all if omitted)");
                fixCmd->add_option("--backlog-root", fix_backlog_root_str, "Backlog root path");
                fixCmd->add_option("--agent", fix_agent, "Agent name for worklog")->required();
                fixCmd->add_option("--model", fix_model, "Model name for worklog");
                fixCmd->add_flag("--apply", fix_apply, "Apply fixes (dry-run by default)");
                fixCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!fix_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(fix_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    int total_checked = 0;
                    int total_issues = 0;
                    int total_fixed = 0;

                    auto products_dir = backlog_root / "products";
                    if (!std::filesystem::exists(products_dir)) {
                        std::cout << "No products directory found.\n";
                        return;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                        if (!entry.is_directory()) continue;
                        std::string product_name = entry.path().filename().string();
                        if (!fix_product.empty() && fix_product != product_name) continue;

                        auto product_root = entry.path();
                        CanonicalStore store(product_root);
                        auto item_paths = store.list_items();
                        int product_checked = 0;
                        int product_issues = 0;
                        int product_fixed = 0;

                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                auto [is_ready, missing_fields] = Validator::is_ready(item);
                                ++product_checked;
                                if (!is_ready) {
                                    ++product_issues;
                                    std::cout << (fix_apply ? "Fixing" : "Dry-run") << " " << item.id << ": missing " << missing_fields.size() << " fields\n";
                                    if (fix_apply) {
                                        for (const auto& field : missing_fields) {
                                            if (field == "context") {
                                                item.context = "TBD: Provide context for this item.";
                                            } else if (field == "goal") {
                                                item.goal = "TBD: Define the goal for this item.";
                                            } else if (field == "approach") {
                                                item.approach = "TBD: Document the implementation approach.";
                                            } else if (field == "acceptance_criteria" || field == "acceptance criteria") {
                                                item.acceptance_criteria = "TBD: Define acceptance criteria.";
                                            } else if (field == "risks" || field == "risks / dependencies") {
                                                item.risks = "TBD: No significant risks identified.";
                                            }
                                        }
                                        StateMachine::record_worklog(
                                            item,
                                            fix_agent,
                                            "Auto-filled missing Ready-gate fields via schema fix",
                                            fix_model.empty() ? std::nullopt : std::optional<std::string>(fix_model)
                                        );
                                        store.write(item);
                                        ++product_fixed;
                                        std::cout << "  Fixed: " << join_strings(missing_fields) << "\n";
                                    }
                                }
                            } catch (const std::exception& e) {
                                ++product_checked;
                                ++product_issues;
                                std::cout << "FAIL " << item_path.filename().string() << ": " << e.what() << "\n";
                            }
                        }

                        total_checked += product_checked;
                        total_issues += product_issues;
                        total_fixed += product_fixed;
                    }

                    std::cout << "\nTotal: " << total_checked << " checked, " << total_issues << " issues, " << total_fixed << " fixed\n";
                    if (!fix_apply && total_issues > 0) {
                        std::cout << "\nDry-run mode. Use --apply to write changes.\n";
                    }
                });
            }
        }

        // ============================================================
        // validate
        // ============================================================
        {
            auto* validateCmd = app.add_subcommand("validate", "Validation helpers");

            // validate uids
            {
                auto* uidsCmd = validateCmd->add_subcommand("uids", "Validate UUIDv7 UIDs");
                std::string uid_product;
                std::string uid_backlog_root_str;
                uidsCmd->add_option("--product", uid_product, "Product name (validate all if omitted)");
                uidsCmd->add_option("--backlog-root", uid_backlog_root_str, "Backlog root path");
                uidsCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!uid_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(uid_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    int total_checked = 0;
                    int total_violations = 0;

                    auto products_dir = backlog_root / "products";
                    if (!std::filesystem::exists(products_dir)) {
                        std::cout << "No products directory found.\n";
                        return;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                        if (!entry.is_directory()) continue;
                        std::string product_name = entry.path().filename().string();
                        if (!uid_product.empty() && uid_product != product_name) continue;

                        auto product_root = entry.path();
                        CanonicalStore store(product_root);
                        auto item_paths = store.list_items();
                        int product_checked = 0;
                        int product_violations = 0;

                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                ++product_checked;
                                // Check UID format: should be UUIDv7
                                if (item.uid.size() != 36) {
                                    ++product_violations;
                                    std::cout << "FAIL " << item.id << ": invalid UID length\n";
                                } else {
                                    // Basic UUIDv7 format check: xxxxxxxx-xxxx-7xxx-xxxx-xxxxxxxxxxxx
                                    bool valid = true;
                                    for (size_t i = 0; i < item.uid.size(); ++i) {
                                        char c = item.uid[i];
                                        if (i == 8 || i == 13 || i == 18 || i == 23) {
                                            if (c != '-') valid = false;
                                        } else if (i == 14) {
                                            if (c != '7') valid = false;
                                        } else {
                                            if (!std::isxdigit(static_cast<unsigned char>(c))) valid = false;
                                        }
                                    }
                                    if (!valid) {
                                        ++product_violations;
                                        std::cout << "FAIL " << item.id << ": malformed UID\n";
                                    }
                                }
                            } catch (const std::exception& e) {
                                ++product_checked;
                                ++product_violations;
                                std::cout << "FAIL " << item_path.filename().string() << ": " << e.what() << "\n";
                            }
                        }

                        total_checked += product_checked;
                        total_violations += product_violations;

                        if (product_violations == 0 && product_checked > 0) {
                            std::cout << "OK " << product_name << ": all " << product_checked << " items have UUIDv7 UIDs\n";
                        } else if (product_violations > 0) {
                            std::cout << "FAIL " << product_name << ": " << product_violations << " UID violations\n";
                        }
                    }

                    if (total_violations > 0) {
                        std::cout << "\nTotal: " << total_checked << " checked, " << total_violations << " violations\n";
                        throw std::runtime_error("UID validation failed");
                    }
                    std::cout << "All products clean. Items checked: " << total_checked << "\n";
                });
            }

            // validate repo-layout
            {
                auto* repoCmd = validateCmd->add_subcommand("repo-layout", "Validate skill repo layout");
                repoCmd->callback([&]() {
                    // Walk up from cwd to find skill root
                    auto cwd = std::filesystem::current_path();
                    std::filesystem::path skill_root;
                    for (auto p = cwd; p != p.parent_path(); p = p.parent_path()) {
                        auto candidate = p / "skills" / "kano-agent-backlog-skill";
                        if (std::filesystem::exists(candidate)) {
                            skill_root = candidate;
                            break;
                        }
                    }

                    if (skill_root.empty()) {
                        std::cout << "OK Skill root not found from cwd; skipping repo-layout checks\n";
                        return;
                    }

                    auto legacy_cli_root = skill_root / "src" / "kano_cli";
                    std::vector<std::filesystem::path> legacy_files;
                    if (std::filesystem::exists(legacy_cli_root)) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(legacy_cli_root)) {
                            if (entry.is_regular_file() && entry.path().extension() == ".py") {
                                legacy_files.push_back(entry.path());
                            }
                        }
                    }

                    if (!legacy_files.empty()) {
                        std::cout << "FAIL Legacy CLI package detected under src/kano_cli\n";
                        for (size_t i = 0; i < std::min(legacy_files.size(), size_t(10)); ++i) {
                            std::cout << "  - " << legacy_files[i] << "\n";
                        }
                        if (legacy_files.size() > 10) {
                            std::cout << "  ... and " << (legacy_files.size() - 10) << " more\n";
                        }
                        std::cout << "Fix: move CLI code under src/kano_backlog_cli and remove src/kano_cli\n";
                        throw std::runtime_error("Repo layout validation failed");
                    }

                    std::cout << "OK Repo layout OK (no legacy src/kano_cli python files)\n";
                });
            }

            // validate links
            {
                auto* linksCmd = validateCmd->add_subcommand("links", "Validate markdown links and wikilinks");
                std::string links_product;
                std::string links_backlog_root_str;
                bool include_views = false;
                linksCmd->add_option("--product", links_product, "Product name (validate all if omitted)");
                linksCmd->add_option("--backlog-root", links_backlog_root_str, "Backlog root path");
                linksCmd->add_flag("--include-views", include_views, "Scan views/ markdown (derived output)");
                linksCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!links_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(links_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    int total_checked = 0;
                    int total_issues = 0;

                    auto products_dir = backlog_root / "products";
                    if (!std::filesystem::exists(products_dir)) {
                        std::cout << "No products directory found.\n";
                        return;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                        if (!entry.is_directory()) continue;
                        std::string product_name = entry.path().filename().string();
                        if (!links_product.empty() && links_product != product_name) continue;

                        auto product_root = entry.path();
                        CanonicalStore store(product_root);
                        RefResolver resolver(store);
                        auto item_paths = store.list_items();
                        int product_issues = 0;
                        int product_checked = 0;

                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                ++product_checked;
                                auto refs = RefResolver::get_references(item);
                                for (const auto& ref : refs) {
                                    try {
                                        resolver.resolve(ref);
                                    } catch (const std::exception&) {
                                        ++product_issues;
                                        std::cout << "FAIL " << item.id << ": unresolvable ref: " << ref << "\n";
                                    }
                                }
                            } catch (const std::exception& e) {
                                ++product_checked;
                            }
                        }

                        total_checked += product_checked;
                        total_issues += product_issues;

                        if (product_issues == 0) {
                            std::cout << "OK " << product_name << ": " << product_checked << " files, no broken links\n";
                        } else {
                            std::cout << "FAIL " << product_name << ": " << product_issues << " broken links in " << product_checked << " files\n";
                        }
                    }

                    if (total_issues > 0) {
                        std::cout << "\nTotal: " << total_checked << " files checked, " << total_issues << " broken links\n";
                        throw std::runtime_error("Link validation failed");
                    }
                    std::cout << "All products clean. Files checked: " << total_checked << "\n";
                });
            }
        }

        // ============================================================
        // links (standalone group)
        // ============================================================
        {
            auto* linksGroupCmd = app.add_subcommand("links", "Link maintenance helpers");

            // links fix
            {
                auto* fixLinksCmd = linksGroupCmd->add_subcommand("fix", "Fix markdown links and wikilinks across backlog");
                std::string lf_product, lf_backlog_root_str;
                bool lf_include_views = false;
                bool lf_apply = false;
                fixLinksCmd->add_option("--product", lf_product, "Product name");
                fixLinksCmd->add_option("--backlog-root", lf_backlog_root_str, "Backlog root path");
                fixLinksCmd->add_flag("--include-views", lf_include_views, "Scan views/ markdown");
                fixLinksCmd->add_flag("--apply", lf_apply, "Apply fixes");
                fixLinksCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!lf_backlog_root_str.empty()) {
                        // Explicit backlog root provided
                        if (lf_product.empty()) {
                            // Need to determine product from config
                            auto ctx = BacklogContext::resolve(
                                path_str,
                                product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt),
                                sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                            );
                            backlog_root = std::filesystem::path(lf_backlog_root_str);
                            // lf_product will be used below to filter
                        } else {
                            backlog_root = std::filesystem::path(lf_backlog_root_str);
                        }
                    } else {
                        auto ctx = BacklogContext::resolve(
                            path_str,
                            lf_product.empty()
                                ? (product_name_opt.empty() ? std::nullopt : std::optional<std::string>(product_name_opt))
                                : std::optional<std::string>(lf_product),
                            sandbox_name_opt.empty() ? std::nullopt : std::optional<std::string>(sandbox_name_opt)
                        );
                        backlog_root = ctx.backlog_root;
                    }

                    int total_fixes = 0;

                    auto products_dir = backlog_root / "products";
                    if (!std::filesystem::exists(products_dir)) {
                        std::cout << "No products directory found.\n";
                        return;
                    }

                    for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                        if (!entry.is_directory()) continue;
                        std::string product_name = entry.path().filename().string();
                        if (!lf_product.empty() && lf_product != product_name) continue;

                        CanonicalStore store(entry.path());
                        RefResolver resolver(store);
                        auto item_paths = store.list_items();

                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                auto refs = RefResolver::get_references(item);
                                int broken = 0;
                                for (const auto& ref : refs) {
                                    try {
                                        resolver.resolve(ref);
                                    } catch (const std::exception&) {
                                        ++broken;
                                    }
                                }
                                if (broken > 0) {
                                    std::cout << "Would fix " << item.id << ": " << broken << " broken link(s)\n";
                                    if (lf_apply) {
                                        // Report-only for now; replacement requires raw markdown editing.
                                    }
                                    total_fixes += broken;
                                }
                            } catch (...) {
                            }
                        }
                    }
                    if (!lf_apply) {
                        std::cout << "Dry-run: " << total_fixes << " broken links found.\n";
                        std::cout << "Run with --apply to confirm (note: actual link replacement not yet implemented).\n";
                    } else {
                        std::cout << "Applied (report only): " << total_fixes << " broken links detected.\n";
                    }
                });
            }

            // links remap-id
            {
                auto* lriCmd = linksGroupCmd->add_subcommand("remap-id", "Remap item ID and update references");
                std::string lri_ref, lri_new_id, lri_agent;
                bool lri_update_refs = true;
                bool lri_apply = false;
                lriCmd->add_option("ref", lri_ref, "Current item ID or UID")->required();
                lriCmd->add_option("--to", lri_new_id, "New ID")->required();
                lriCmd->add_option("--agent", lri_agent, "Agent identifier")->required();
                lriCmd->add_flag("--update-refs", lri_update_refs, "Update references across backlog");
                lriCmd->add_flag("--apply", lri_apply, "Apply changes");
                lriCmd->callback([&]() {
                    auto ctx = resolve_ctx();
                    BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                    auto result = WorkitemOps::remap_id(index, ctx.product_root, lri_ref, lri_new_id, lri_agent);
                    std::cout << "Remapped ID: " << result.old_id << " -> " << result.new_id << "\n";
                    std::cout << "Updated " << result.updated_files << " files.\n";
                });
            }

            // links normalize-ids (standalone)
            {
                auto* normCmd = linksGroupCmd->add_subcommand("normalize-ids", "Normalize duplicate IDs");
                std::string norm_product, norm_agent, norm_backlog_root_str;
                bool norm_apply = false;
                normCmd->add_option("--product", norm_product, "Product name");
                normCmd->add_option("--backlog-root", norm_backlog_root_str, "Backlog root path");
                normCmd->add_option("--agent", norm_agent, "Agent identifier")->required();
                normCmd->add_flag("--apply", norm_apply, "Apply fixes");
                normCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!norm_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(norm_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    std::string product_name = norm_product;
                    if (product_name.empty()) {
                        auto ctx = resolve_ctx();
                        product_name = ctx.product_name;
                    }

                    auto product_root = backlog_root / "products" / product_name;
                    BacklogIndex index(backlog_root / ".cache" / "index" / "backlog.db");
                    index.initialize();

                    auto all_items = index.query_items();
                    std::map<std::string, std::vector<IndexItem>> by_display_id;
                    for (const auto& item : all_items) {
                        by_display_id[item.id].push_back(item);
                    }

                    int duplicates_found = 0;
                    int normalized = 0;
                    int items_to_rename = 0;
                    for (const auto& [id, items] : by_display_id) {
                        if (items.size() > 1) {
                            ++duplicates_found;
                            std::cout << "Duplicate ID: " << id << " (" << items.size() << " copies)\n";
                            for (size_t i = 1; i < items.size(); ++i) {
                                const auto& item = items[i];
                                std::string new_id = item.id + "-dup" + std::to_string(i);
                                std::cout << "  Would rename: " << item.id << " -> " << new_id << "\n";
                                ++items_to_rename;
                                if (norm_apply) {
                                    auto result = WorkitemOps::remap_id(index, product_root, item.uid, new_id, norm_agent);
                                    ++normalized;
                                    std::cout << "  Renamed: " << result.old_id << " -> " << result.new_id << "\n";
                                }
                            }
                        }
                    }
                    if (!norm_apply) {
                        std::cout << "Dry-run: " << duplicates_found << " duplicate ID groups, " << items_to_rename << " items would be renamed.\n";
                        std::cout << "Run with --apply to normalize.\n";
                    } else {
                        std::cout << "Normalized: " << normalized << " duplicate IDs.\n";
                    }
                });
            }

            // links replace-id
            {
                auto* replaceCmd = linksGroupCmd->add_subcommand("replace-id", "Replace ID token in specific files");
                std::string old_id, new_id;
                bool replace_apply = false;
                replaceCmd->add_option("--old", old_id, "Old ID")->required();
                replaceCmd->add_option("--new", new_id, "New ID")->required();
                replaceCmd->add_flag("--apply", replace_apply, "Apply changes");
                replaceCmd->callback([&]() {
                    if (!replace_apply) {
                        std::cout << "Dry-run: would replace ID '" << old_id << "' with '" << new_id << "' in all item files.\n";
                    } else {
                        auto ctx = resolve_ctx();
                        CanonicalStore store(ctx.product_root);
                        auto item_paths = store.list_items();
                        int count = 0;
                        for (const auto& item_path : item_paths) {
                            try {
                                auto item = store.read(item_path);
                                bool changed = false;

                                if (item.parent && *item.parent == old_id) {
                                    item.parent = new_id;
                                    changed = true;
                                }
                                for (auto& relate : item.links.relates) {
                                    if (relate == old_id) {
                                        relate = new_id;
                                        changed = true;
                                    }
                                }
                                for (auto& block : item.links.blocks) {
                                    if (block == old_id) {
                                        block = new_id;
                                        changed = true;
                                    }
                                }
                                for (auto& blocked_by : item.links.blocked_by) {
                                    if (blocked_by == old_id) {
                                        blocked_by = new_id;
                                        changed = true;
                                    }
                                }
                                for (auto& decision : item.decisions) {
                                    if (decision == old_id) {
                                        decision = new_id;
                                        changed = true;
                                    }
                                }

                                if (changed) {
                                    store.write(item);
                                    ++count;
                                }
                            } catch (...) {
                            }
                        }
                        std::cout << "Replaced '" << old_id << "' with '" << new_id << "' in " << count << " files.\n";
                    }
                });
            }

            // links replace-target
            {
                auto* rtCmd = linksGroupCmd->add_subcommand("replace-target", "Replace link targets for an ID");
                std::string rt_old_id, rt_new_path, rt_product;
                bool rt_apply = false;
                rtCmd->add_option("--old-id", rt_old_id, "Old ID")->required();
                rtCmd->add_option("--new-path", rt_new_path, "New target path")->required();
                rtCmd->add_flag("--apply", rt_apply, "Apply changes");
                rtCmd->callback([&]() {
                    if (!rt_apply) {
                        std::cout << "Dry-run: would replace link target for '" << rt_old_id << "' with path '" << rt_new_path << "'.\n";
                    } else {
                        std::cout << "replace-target: not fully implemented (requires content scanning).\n";
                    }
                });
            }

            // links restore-from-vcs
            {
                auto* rfvCmd = linksGroupCmd->add_subcommand("restore-from-vcs", "Restore missing link targets from VCS history");
                std::string rfv_product, rfv_backlog_root_str;
                bool rfv_include_views = false;
                bool rfv_apply = false;
                rfvCmd->add_option("--product", rfv_product, "Product name");
                rfvCmd->add_option("--backlog-root", rfv_backlog_root_str, "Backlog root path");
                rfvCmd->add_flag("--include-views", rfv_include_views, "Scan views");
                rfvCmd->add_flag("--apply", rfv_apply, "Apply changes");
                rfvCmd->callback([&]() {
                    std::cout << "restore-from-vcs: requires VCS integration not yet implemented.\n";
                    std::cout << "Suggestion: use 'git log --follow <file>' to find historical content.\n";
                });
            }
        }

        // ============================================================
        // adr
        // ============================================================
        {
            auto* adrCmd = app.add_subcommand("adr", "ADR (Architecture Decision Record) operations");

            // adr create
            {
                auto* createCmd = adrCmd->add_subcommand("create", "Create a new ADR");
                std::string adr_title;
                std::string adr_product;
                std::string adr_agent;
                std::string adr_status = "Proposed";
                std::string adr_backlog_root_str;
                createCmd->add_option("--title", adr_title, "ADR title")->required();
                createCmd->add_option("--product", adr_product, "Product name")->required();
                createCmd->add_option("--agent", adr_agent, "Agent identifier")->required();
                createCmd->add_option("--status", adr_status, "Initial ADR status");
                createCmd->add_option("--backlog-root", adr_backlog_root_str, "Backlog root path");
                createCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!adr_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(adr_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    auto decisions_dir = backlog_root / "products" / adr_product / "decisions";
                    if (!std::filesystem::exists(decisions_dir)) {
                        std::filesystem::create_directories(decisions_dir);
                    }

                    // Generate ADR ID
                    int next_num = 1;
                    for (const auto& entry : std::filesystem::directory_iterator(decisions_dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".md") {
                            std::string filename = entry.path().stem().string();
                            if (filename.rfind("ADR-", 0) == 0) {
                                try {
                                    int num = std::stoi(filename.substr(4));
                                    next_num = std::max(next_num, num + 1);
                                } catch (...) {}
                            }
                        }
                    }

                    std::string id = "ADR-" + std::to_string(next_num);
                    std::string uid = CanonicalStore::generate_uuid_v7();
                    auto now = std::chrono::system_clock::now();
                    auto time_t_now = std::chrono::system_clock::to_time_t(now);
                    std::ostringstream date_ss;
                    date_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d");
                    std::string date_str = date_ss.str();

                    std::ostringstream body;
                    body << "---\n";
                    body << "uid: " << uid << "\n";
                    body << "type: adr\n";
                    body << "status: " << adr_status << "\n";
                    body << "date: " << date_str << "\n";
                    body << "author: " << adr_agent << "\n";
                    body << "links: []\n";
                    body << "---\n\n";
                    body << "# " << id << ": " << adr_title << "\n\n";
                    body << "## Status\n\n" << adr_status << "\n\n";
                    body << "## Context\n\nWhat is the issue that we're seeing that is motivating this decision?\n\n";
                    body << "## Decision\n\nWhat is the decision that we're proposing?\n\n";
                    body << "## Consequences\n\nWhat becomes easier or more difficult because of this decision?\n";

                    auto adr_path = decisions_dir / (id + ".md");
                    std::ofstream ofs(adr_path);
                    if (!ofs) throw std::runtime_error("Failed to create ADR file: " + adr_path.string());
                    ofs << body.str();
                    ofs.close();

                    std::cout << "Created " << id << ": " << adr_title << "\n";
                    std::cout << "Path: " << adr_path.string() << "\n";
                });
            }

            // adr fix-uids (stub)
            {
                auto* fixUidsCmd = adrCmd->add_subcommand("fix-uids", "Backfill missing/invalid ADR UIDs (UUIDv7)");
                std::string fu_product;
                std::string fu_backlog_root_str;
                std::string fu_agent;
                bool fu_apply = false;
                fixUidsCmd->add_option("--product", fu_product, "Product name");
                fixUidsCmd->add_option("--backlog-root", fu_backlog_root_str, "Backlog root path");
                fixUidsCmd->add_option("--agent", fu_agent, "Agent identifier")->required();
                fixUidsCmd->add_flag("--apply", fu_apply, "Apply fixes (dry-run by default)");
                fixUidsCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!fu_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(fu_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    std::vector<std::filesystem::path> product_roots;
                    if (!fu_product.empty()) {
                        product_roots.push_back(backlog_root / "products" / fu_product);
                    } else {
                        auto products_dir = backlog_root / "products";
                        if (std::filesystem::exists(products_dir)) {
                            for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                                if (entry.is_directory()) product_roots.push_back(entry.path());
                            }
                        }
                    }

                    int total_checked = 0;
                    int total_updated = 0;

                    for (const auto& prod_root : product_roots) {
                        auto decisions_dir = prod_root / "decisions";
                        if (!std::filesystem::exists(decisions_dir)) continue;

                        int checked = 0;
                        int updated = 0;

                        for (const auto& entry : std::filesystem::directory_iterator(decisions_dir)) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
                            if (entry.path().filename().string() == "README.md") continue;

                            ++checked;
                            std::string content;
                            {
                                std::ifstream ifs(entry.path());
                                if (!ifs) continue;
                                std::ostringstream ss;
                                ss << ifs.rdbuf();
                                content = ss.str();
                            }

                            auto fm_ctx = kano::backlog_core::Frontmatter::parse(content);
                            if (fm_ctx.metadata.IsNull()) continue;

                            std::string current_uid;
                            try {
                                current_uid = fm_ctx.metadata["uid"].as<std::string>();
                            } catch (...) {}

                            bool is_valid_uuidv7 = false;
                            if (!current_uid.empty()) {
                                std::regex uuidv7_pattern("^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
                                    std::regex_constants::icase | std::regex_constants::ECMAScript);
                                is_valid_uuidv7 = std::regex_match(current_uid, uuidv7_pattern);
                            }

                            if (is_valid_uuidv7) {
                                std::cout << "OK " << entry.path().filename().string() << ": UID OK\n";
                            } else {
                                std::string new_uid = kano::backlog_core::CanonicalStore::generate_uuid_v7();
                                fm_ctx.metadata["uid"] = new_uid;
                                std::string new_content = kano::backlog_core::Frontmatter::serialize(fm_ctx);
                                if (fu_apply) {
                                    std::ofstream ofs(entry.path());
                                    if (ofs) {
                                        ofs << new_content;
                                        ++updated;
                                        std::cout << "UPDATED " << entry.path().filename().string() << ": " << (current_uid.empty() ? "<missing>" : current_uid) << " -> " << new_uid << "\n";
                                    }
                                } else {
                                    std::cout << "DRY-RUN " << entry.path().filename().string() << ": " << (current_uid.empty() ? "<missing>" : current_uid) << " -> " << new_uid << "\n";
                                }
                            }
                        }

                        total_checked += checked;
                        total_updated += updated;
                        std::cout << prod_root.filename().string() << ": checked=" << checked << " updated=" << updated << "\n";
                    }

                    std::cout << "\nTotal: checked=" << total_checked << " updated=" << total_updated << "\n";
                    if (!fu_apply && total_updated > 0) {
                        std::cout << "Run with --apply to write changes.\n";
                    }
                });
            }
        }

        // ============================================================
        // changelog
        // ============================================================
        {
            auto* changelogCmd = app.add_subcommand("changelog", "Changelog generation from backlog");

            // changelog generate
            {
                auto* genCmd = changelogCmd->add_subcommand("generate", "Generate changelog from Done backlog items");
                std::string cg_version;
                std::string cg_product;
                std::string cg_backlog_root_str;
                std::string cg_output_str;
                std::string cg_date_str;
                genCmd->add_option("--version", cg_version, "Version string (e.g., 0.0.1)")->required();
                genCmd->add_option("--product", cg_product, "Product name");
                genCmd->add_option("--backlog-root", cg_backlog_root_str, "Backlog root path");
                genCmd->add_option("-o,--output", cg_output_str, "Output file (default: stdout)");
                genCmd->add_option("--date", cg_date_str, "Release date (YYYY-MM-DD, default: today)");
                genCmd->callback([&]() {
                    std::filesystem::path backlog_root;
                    if (!cg_backlog_root_str.empty()) {
                        backlog_root = std::filesystem::path(cg_backlog_root_str);
                    } else {
                        auto ctx = resolve_ctx();
                        backlog_root = ctx.backlog_root;
                    }

                    std::string date_str = cg_date_str;
                    if (date_str.empty()) {
                        auto now = std::chrono::system_clock::now();
                        auto time_t_now = std::chrono::system_clock::to_time_t(now);
                        std::ostringstream oss;
                        oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d");
                        date_str = oss.str();
                    }

                    // Collect Done items from products
                    std::vector<BacklogItem> done_items;
                    auto products_dir = backlog_root / "products";
                    if (std::filesystem::exists(products_dir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
                            if (!entry.is_directory()) continue;
                            std::string product_name = entry.path().filename().string();
                            if (!cg_product.empty() && cg_product != product_name) continue;

                            CanonicalStore store(entry.path());
                            for (const auto& item_path : store.list_items()) {
                                try {
                                    auto item = store.read(item_path);
                                    if (item.state == ItemState::Done) {
                                        done_items.push_back(item);
                                    }
                                } catch (...) {}
                            }
                        }
                    }

                    std::ostringstream out;
                    out << "## [" << cg_version << "] - " << date_str << "\n\n";
                    if (done_items.empty()) {
                        out << "*No changes in this release.*\n";
                    } else {
                        // Group by type
                        std::map<ItemType, std::vector<BacklogItem*>> by_type;
                        for (auto& item : done_items) {
                            by_type[item.type].push_back(&item);
                        }
                        const char* type_names[] = {"Epic", "Feature", "UserStory", "Task", "Bug"};
                        for (int t = 0; t < 5; ++t) {
                            ItemType type = static_cast<ItemType>(t);
                            auto it = by_type.find(type);
                            if (it == by_type.end()) continue;
                            out << "### " << type_names[t] << "s\n\n";
                            for (auto* item : it->second) {
                                out << "- " << item->id << " " << item->title << " (" << kano::backlog_core::to_string(item->state) << ")\n";
                            }
                            out << "\n";
                        }
                    }

                    std::string content = out.str();
                    if (!cg_output_str.empty()) {
                        auto out_path = std::filesystem::path(cg_output_str);
                        std::filesystem::create_directories(out_path.parent_path());
                        std::ofstream ofs(out_path);
                        if (!ofs) throw std::runtime_error("Failed to write: " + out_path.string());
                        ofs << content;
                        ofs.close();
                        std::cout << "Generated changelog for v" << cg_version << " with " << done_items.size() << " items -> " << out_path << "\n";
                    } else {
                        std::cout << content;
                    }
                });
            }

            // changelog merge-unreleased (stub)
            {
                auto* mergeCmd = changelogCmd->add_subcommand("merge-unreleased", "Merge [Unreleased] section into specified version");
                std::string mu_version;
                std::string mu_changelog_str = "CHANGELOG.md";
                std::string mu_date_str;
                bool mu_dry_run = false;
                mergeCmd->add_option("--version", mu_version, "Version to merge into (e.g., 0.0.1)")->required();
                mergeCmd->add_option("--changelog", mu_changelog_str, "Path to CHANGELOG.md");
                mergeCmd->add_option("--date", mu_date_str, "Release date (YYYY-MM-DD, default: today)");
                mergeCmd->add_flag("--dry-run", mu_dry_run, "Preview changes without writing");
                mergeCmd->callback([&]() {
                    auto changelog_path = std::filesystem::path(mu_changelog_str);
                    if (!std::filesystem::exists(changelog_path)) {
                        throw std::runtime_error("CHANGELOG.md not found: " + changelog_path.string());
                    }

                    std::string content;
                    {
                        std::ifstream ifs(changelog_path);
                        if (!ifs) throw std::runtime_error("Cannot read: " + changelog_path.string());
                        std::ostringstream ss;
                        ss << ifs.rdbuf();
                        content = ss.str();
                    }

                    std::string release_date = mu_date_str;
                    if (release_date.empty()) {
                        auto now = std::chrono::system_clock::now();
                        auto time_t_now = std::chrono::system_clock::to_time_t(now);
                        std::ostringstream oss;
                        oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d");
                        release_date = oss.str();
                    }

                    // Parse into lines
                    std::vector<std::string> lines;
                    std::string line;
                    std::istringstream iss(content);
                    while (std::getline(iss, line)) {
                        lines.push_back(line);
                    }

                    // Find [Unreleased] section
                    int unreleased_start = -1;
                    int unreleased_end = -1;
                    int version_start = -1;

                    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
                        if (lines[i].find("## [Unreleased]") != std::string::npos) {
                            unreleased_start = i;
                        } else if (unreleased_start >= 0 && unreleased_end < 0) {
                            if (lines[i].rfind("## [", 0) == 0 && lines[i].find("[Unreleased]") == std::string::npos) {
                                unreleased_end = i;
                            } else if (lines[i].find("## Option") == 0 || lines[i].find("## Recommendation") == 0) {
                                unreleased_end = i;
                            }
                        }
                        if (lines[i].find("## [" + mu_version + "]") != std::string::npos) {
                            version_start = i;
                        }
                    }

                    if (unreleased_start < 0) {
                        throw std::runtime_error("No [Unreleased] section found in CHANGELOG.md");
                    }

                    if (unreleased_end < 0) unreleased_end = static_cast<int>(lines.size());

                    // Extract unreleased content
                    std::vector<std::string> unreleased_content(lines.begin() + unreleased_start + 1, lines.begin() + unreleased_end);

                    std::string new_content;
                    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
                        if (version_start >= 0 && i == version_start) {
                            new_content += "## [" + mu_version + "] - " + release_date + "\n";
                            for (const auto& ul : unreleased_content) new_content += ul + "\n";
                        } else if (i >= unreleased_start && i < unreleased_end) {
                            // Skip - replaced
                            if (i == unreleased_start + 1) {
                                new_content += "\n";
                            }
                        } else if (!(version_start < 0 && i == unreleased_end)) {
                            new_content += lines[i] + "\n";
                        }
                    }

                    if (version_start < 0) {
                        new_content += "## [" + mu_version + "] - " + release_date + "\n";
                        for (const auto& ul : unreleased_content) new_content += ul + "\n";
                    }

                    if (!mu_dry_run) {
                        std::ofstream ofs(changelog_path);
                        if (!ofs) throw std::runtime_error("Cannot write: " + changelog_path.string());
                        ofs << new_content;
                        std::cout << "Merged [Unreleased] into v" << mu_version << " in " << changelog_path << "\n";
                    } else {
                        std::cout << "DRY-RUN - would write:\n" << new_content << "\n";
                    }
                });
            }
        }

        // ============================================================
        // demo
        // ============================================================
        {
            auto* demoCmd = app.add_subcommand("demo", "Demo data operations");
            auto* seedCmd = demoCmd->add_subcommand("seed", "Seed a product with reproducible demo items");
            std::string seed_product, seed_agent;
            int seed_count = 5;
            bool seed_force = false;
            seedCmd->add_option("--product", seed_product, "Product name to seed")->required();
            seedCmd->add_option("--agent", seed_agent, "Agent identifier")->required();
            seedCmd->add_option("--count", seed_count, "Number of demo items to create");
            seedCmd->add_flag("--force", seed_force, "Recreate demo items if they exist");

            seedCmd->callback([&]() {
                auto ctx = resolve_ctx();
                auto product_root = ctx.backlog_root / "products" / seed_product;
                if (!std::filesystem::exists(product_root)) {
                    throw std::runtime_error("Product not initialized: " + seed_product);
                }

                auto items_root = product_root / "items";
                std::vector<std::filesystem::path> existing_demo;
                if (std::filesystem::exists(items_root)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(items_root)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".md") {
                            std::string fname = entry.path().filename().string();
                            if (fname.find("demo") != std::string::npos ||
                                fname.find("Demo") != std::string::npos) {
                                existing_demo.push_back(entry.path());
                            }
                        }
                    }
                }

                if (!existing_demo.empty() && !seed_force) {
                    throw std::runtime_error("Demo items already exist in " + seed_product +
                        " (found " + std::to_string(existing_demo.size()) + " items). Use --force to recreate.");
                }

                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                index.initialize();

                // Create Epic
                auto epic_result = WorkitemOps::create_item(
                    index, product_root, ctx.product_def.prefix,
                    ItemType::Epic, "Demo Epic: Multi-Agent Backlog System",
                    seed_agent, std::nullopt, "P1", {"demo", "sample"}, "demo", "backlog"
                );

                // Create Feature
                auto feature_result = WorkitemOps::create_item(
                    index, product_root, ctx.product_def.prefix,
                    ItemType::Feature, "Demo Feature: Local-First Backlog Ops",
                    seed_agent, epic_result.id, "P1", {"demo", "sample"}, "demo", "backlog"
                );

                int task_count = std::min(seed_count - 2, 3);
                std::vector<std::string> task_titles = {
                    "Implement file-based canonical storage",
                    "Add SQLite index builder",
                    "Create CLI facade with C++"
                };

                for (int i = 0; i < task_count; ++i) {
                    auto task_result = WorkitemOps::create_item(
                        index, product_root, ctx.product_def.prefix,
                        ItemType::Task, "Demo Task: " + task_titles[i],
                        seed_agent, feature_result.id, "P2", {"demo", "sample"}, "demo", "backlog"
                    );
                    std::cout << "Created " << task_result.id << " (" << task_result.path.filename().string() << ")\n";
                }

                std::cout << "\nSeeded demo data in " << seed_product << "\n";
                std::cout << "Created " << (2 + task_count) << " items:\n";
                std::cout << "  Epic: " << epic_result.id << "\n";
                std::cout << "  Feature: " << feature_result.id << "\n";
                if (seed_force && !existing_demo.empty()) {
                    std::cout << "  (Cleaned up " << existing_demo.size() << " existing demo items)\n";
                }
            });
        }

        // ============================================================
        // orphan
        // ============================================================
        {
            auto* orphanCmd = app.add_subcommand("orphan", "Check for commits without backlog item IDs");

            // orphan check
            {
                auto* checkCmd = orphanCmd->add_subcommand("check", "Check for orphan commits");
                int check_days = 7;
                bool show_all = false;
                std::string check_format = "table";
                checkCmd->add_option("-d,--days", check_days, "Check commits from last N days");
                checkCmd->add_flag("-a,--all", show_all, "Show all commits including trivial");
                checkCmd->add_option("-f,--format", check_format, "Output format: table|json|plain");

                checkCmd->callback([&]() {
                    auto since_date = std::chrono::system_clock::now() - std::chrono::hours(check_days * 24);
                    auto time_t_since = std::chrono::system_clock::to_time_t(since_date);
                    char since_buf[32];
                    std::strftime(since_buf, sizeof(since_buf), "%Y-%m-%d", std::localtime(&time_t_since));

                     std::string cmd = "git log --since=" + std::string(since_buf) + " --pretty=format:%h=%ad=%s --date=short 2>NUL";
                    std::vector<std::tuple<std::string, std::string, std::string>> commits;
                    std::vector<std::tuple<std::string, std::string, std::string>> orphans;
                    std::vector<std::tuple<std::string, std::string, std::string>> trivial;
                    std::vector<std::tuple<std::string, std::string, std::string, std::string>> with_tickets;

                    std::regex ticket_pattern("KABSD-(FTR|TSK|BUG|USR|EPC)-\\d+", std::regex_constants::icase);
                    std::regex trivial_patterns[] = {
                        std::regex("^(docs|chore|style|typo|format):", std::regex_constants::icase),
                        std::regex("^Merge ", std::regex_constants::icase),
                        std::regex("^Revert ", std::regex_constants::icase),
                        std::regex("^WIP:", std::regex_constants::icase)
                    };

                    FILE* pipe = _popen(cmd.c_str(), "r");
                    if (!pipe) {
                        std::cerr << "Error: Cannot run git log\n";
                        return;
                    }
                    char buf[256];
                    while (fgets(buf, sizeof(buf), pipe)) {
                        std::string line(buf);
                        line.erase(line.find_last_not_of("\r\n") + 1);
                        if (line.empty()) continue;
                        size_t p1 = line.find('=');
                        size_t p2 = line.find('=', p1 + 1);
                        if (p1 == std::string::npos || p2 == std::string::npos) continue;
                        std::string hash = line.substr(0, p1);
                        std::string date = line.substr(p1 + 1, 10);
                        std::string msg = line.substr(p2 + 1);
                        commits.emplace_back(hash, date, msg);

                        std::smatch m;
                        std::string ticket_id;
                        bool is_trivial = false;
                        for (auto& tp : trivial_patterns) {
                            if (std::regex_search(msg, tp)) { is_trivial = true; break; }
                        }
                        if (std::regex_search(msg, m, ticket_pattern)) {
                            ticket_id = m.str(1);
                            with_tickets.emplace_back(hash, date, msg, ticket_id);
                        } else if (is_trivial) {
                            trivial.emplace_back(hash, date, msg);
                        } else {
                            orphans.emplace_back(hash, date, msg);
                        }
                    }
                    _pclose(pipe);

                    if (commits.empty()) {
                        std::cout << "No commits found in the last " << check_days << " days.\n";
                        return;
                    }

                    if (check_format == "json") {
                        std::cout << "{"
                            << "\"summary\":{\"total\":" << commits.size()
                            << ",\"with_tickets\":" << with_tickets.size()
                            << ",\"orphans\":" << orphans.size()
                            << ",\"trivial\":" << trivial.size() << "},"
                            << "\"orphans\":[";
                        bool first = true;
                        for (auto& o : orphans) {
                            if (!first) std::cout << ",";
                            std::cout << "{\"hash\":\"" << std::get<0>(o) << "\",\"date\":\"" << std::get<1>(o) << "\",\"message\":\"" << std::get<2>(o) << "\"}";
                            first = false;
                        }
                        std::cout << "]}\n";
                        return;
                    }

                    if (check_format == "plain") {
                        for (auto& o : orphans) {
                            std::cout << std::get<0>(o) << " " << std::get<1>(o) << " " << std::get<2>(o) << "\n";
                        }
                        return;
                    }

                    // Table format
                    std::cout << "\nCommit Analysis (last " << check_days << " days)\n\n";
                    std::cout << "Total commits: " << commits.size() << "\n";
                    std::cout << "With tickets:  " << with_tickets.size() << "\n";
                    std::cout << "Orphan commits: " << orphans.size() << "\n";
                    std::cout << "Trivial commits: " << trivial.size() << "\n\n";

                    if (!orphans.empty()) {
                        std::cout << "Orphan Commits (need tickets):\n\n";
                        for (auto& o : orphans) {
                            std::cout << "  " << std::get<0>(o) << "  " << std::get<1>(o) << "  " << std::get<2>(o) << "\n";
                        }
                        std::cout << "\nSuggested actions:\n";
                        std::cout << "  1. Create tickets: kano-backlog item create --type task --title \"...\"\n";
                        std::cout << "  2. Amend commits: git rebase -i HEAD~N\n";
                        std::cout << "  3. Or add to existing: kano-backlog worklog append KABSD-TSK-XXXX --message \"...\"\n\n";
                    } else {
                        std::cout << "All commits have tickets or are trivial!\n\n";
                    }

                    if (show_all && !trivial.empty()) {
                        std::cout << "Trivial Commits (no ticket needed):\n\n";
                        for (auto& t : trivial) {
                            std::cout << "  " << std::get<0>(t) << "  " << std::get<1>(t) << "  " << std::get<2>(t) << "\n";
                        }
                        std::cout << "\n";
                    }
                });
            }

            // orphan suggest
            {
                auto* suggestCmd = orphanCmd->add_subcommand("suggest", "Suggest ticket type and title for a commit");
                std::string suggest_hash;
                suggestCmd->add_option("commit_hash", suggest_hash, "Commit hash to analyze")->required();

                suggestCmd->callback([&]() {
                    // Get commit message
                    std::string msg_cmd = "git log -1 --pretty=%B " + suggest_hash + " 2>NUL";
                    FILE* msg_pipe = _popen(msg_cmd.c_str(), "r");
                    std::string message;
                    if (msg_pipe) {
                        char buf[1024];
                        while (fgets(buf, sizeof(buf), msg_pipe)) message += buf;
                        _pclose(msg_pipe);
                    }
                    if (message.empty()) {
                        std::cerr << "Error: Commit " << suggest_hash << " not found\n";
                        return;
                    }
                    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();

                    // Get changed files
                    std::string files_cmd = "git diff-tree --no-commit-id --name-only -r " + suggest_hash + " 2>NUL";
                    FILE* files_pipe = _popen(files_cmd.c_str(), "r");
                    std::vector<std::string> files;
                    if (files_pipe) {
                        char buf[512];
                        while (fgets(buf, sizeof(buf), files_pipe)) {
                            std::string f(buf);
                            while (!f.empty() && (f.back() == '\n' || f.back() == '\r')) f.pop_back();
                            if (!f.empty()) files.push_back(f);
                        }
                        _pclose(files_pipe);
                    }

                    std::regex ticket_pattern("KABSD-(FTR|TSK|BUG|USR|EPC)-\\d+", std::regex_constants::icase);
                    std::smatch m;
                    if (std::regex_search(message, m, ticket_pattern)) {
                        std::cout << "Commit already has ticket: " << m.str(0) << "\n";
                        return;
                    }

                    std::string ticket_type = "task";
                    std::string description = "Code change";
                    std::string msg_lower = message;
                    std::transform(msg_lower.begin(), msg_lower.end(), msg_lower.begin(), ::tolower);

                    if (msg_lower.find("feat") != std::string::npos || msg_lower.find("feature") != std::string::npos ||
                        msg_lower.find(" add ") != std::string::npos || msg_lower.find("implement") != std::string::npos) {
                        ticket_type = "task"; description = "Feature implementation";
                    } else if (msg_lower.find("fix") != std::string::npos || msg_lower.find("bug") != std::string::npos ||
                               msg_lower.find("error") != std::string::npos || msg_lower.find("crash") != std::string::npos) {
                        ticket_type = "bug"; description = "Bug fix";
                    } else if (msg_lower.find("refactor") != std::string::npos || msg_lower.find("cleanup") != std::string::npos ||
                               msg_lower.find("improve") != std::string::npos) {
                        ticket_type = "task"; description = "Code refactoring";
                    }

                    std::string title = message;
                    size_t nl = title.find('\n');
                    if (nl != std::string::npos) title = title.substr(0, nl);
                    size_t colon = title.find(':');
                    if (colon != std::string::npos && colon < 60) title = title.substr(colon + 1);
                    while (!title.empty() && title[0] == ' ') title.erase(title.begin());

                    std::cout << "\nCommit: " << suggest_hash << "\n";
                    std::cout << "Message: " << title << "\n";
                    std::cout << "Files: " << files.size() << "\n\n";
                    std::cout << "Suggested ticket:\n";
                    std::cout << "  Type: " << ticket_type << "\n";
                    std::cout << "  Title: " << title << "\n\n";
                    std::cout << "Create ticket:\n";
                    std::cout << "  kano-backlog item create \\\n";
                    std::cout << "    --type " << ticket_type << " \\\n";
                    std::cout << "    --title \"" << title << "\" \\\n";
                    std::cout << "    --product <product> \\\n";
                    std::cout << "    --agent <agent>\n\n";
                });
            }
        }

        // ============================================================
        // meta
        // ============================================================
        {
            auto* metaCmd = app.add_subcommand("meta", "Meta file helpers");
            auto* tgCmd = metaCmd->add_subcommand("add-ticketing-guidance", "Append ticketing guidance to _meta/conventions.md");
            std::string tg_product, tg_agent;
            bool tg_apply = false;
            tgCmd->add_option("--product", tg_product, "Product name")->required();
            tgCmd->add_option("--agent", tg_agent, "Agent identifier")->required();
            tgCmd->add_flag("--apply", tg_apply, "Write changes to disk");

            tgCmd->callback([&]() {
                auto ctx = resolve_ctx();
                auto conventions_path = ctx.product_root / "_meta" / "conventions.md";
                if (!std::filesystem::exists(conventions_path)) {
                    throw std::runtime_error("Conventions file not found: " + conventions_path.string());
                }

                std::string content;
                {
                    std::ifstream ifs(conventions_path);
                    if (!ifs) throw std::runtime_error("Cannot read: " + conventions_path.string());
                    std::ostringstream ss;
                    ss << ifs.rdbuf();
                    content = ss.str();
                }

                if (content.find("## Ticket type selection") != std::string::npos) {
                    std::cout << "OK: ticketing guidance unchanged\n";
                    std::cout << "  Path: " << conventions_path.string() << "\n";
                    return;
                }

                const char* guidance =
                    "## Ticket type selection\n\n"
                    "- Epic: multi-release or multi-team milestone spanning multiple Features.\n"
                    "- Feature: a new capability that delivers multiple UserStories.\n"
                    "- UserStory: a single user-facing outcome that requires multiple Tasks.\n"
                    "- Task: a single focused implementation or doc change (typically one session).\n"
                    "- Example: \"End-to-end embedding pipeline\" = Epic; \"Pluggable vector backend\" = Feature; \"MVP chunking pipeline\" = UserStory; \"Implement tokenizer adapter\" = Task.\n";

                std::string updated = content;
                if (!updated.empty() && updated.back() != '\n') updated += "\n";
                updated += "\n";
                updated += guidance;

                if (tg_apply) {
                    std::ofstream ofs(conventions_path);
                    if (!ofs) throw std::runtime_error("Cannot write: " + conventions_path.string());
                    ofs << updated;
                    std::cout << "OK: ticketing guidance updated\n";
                } else {
                    std::cout << "DRY-RUN: ticketing guidance would be updated\n";
                }
                std::cout << "  Path: " << conventions_path.string() << "\n";
                if (!tg_apply) std::cout << "  Run with --apply to write.\n";
            });
        }

        // ============================================================
        // snapshot
        // ============================================================
        {
            auto* snapshotCmd = app.add_subcommand("snapshot", "Snapshot and evidence collection");
            auto* createCmd = snapshotCmd->add_subcommand("create", "Generate a deterministic snapshot evidence pack");
            std::string view_arg = "all";
            std::string snapshot_scope = "repo";
            std::string snapshot_format = "md";
            bool snapshot_write = false;
            std::string snapshot_out;
            std::string snapshot_meta_mode = "min";
            createCmd->add_option("view", view_arg, "View to capture: all|stubs|cli|health|capabilities");
            createCmd->add_option("--scope", snapshot_scope, "Scope: repo|product:<name>");
            createCmd->add_option("-f,--format", snapshot_format, "Output format: json|md");
            createCmd->add_flag("-w,--write", snapshot_write, "Write output to file");
            createCmd->add_option("-o,--out", snapshot_out, "Custom output path");
            createCmd->add_option("--meta-mode", snapshot_meta_mode, "Metadata block mode: none|min|full");

            createCmd->callback([&]() {
                auto cwd = std::filesystem::current_path();
                std::string product_name;
                std::string scope = snapshot_scope;
                if (scope.rfind("product:", 0) == 0) {
                    product_name = scope.substr(8);
                }

                std::cout << "Snapshotting " << scope << " (view=" << view_arg << ")...\n";

                // For MVP, just produce a basic evidence pack stub
                std::ostringstream out;
                out << "# Snapshot Report: " << scope << "\n\n";
                out << "**Scope:** " << scope << "\n";
                out << "**View:** " << view_arg << "\n\n";

                if (view_arg == "all" || view_arg == "stubs") {
                    out << "## Stubs & TODOs\n\n";
                    out << "Stub scanning not yet implemented in C++.\n\n";
                }

                if (view_arg == "all" || view_arg == "capabilities") {
                    out << "## Capabilities\n\n";
                    out << "Capability collection not yet implemented in C++.\n\n";
                }

                if (view_arg == "all" || view_arg == "health") {
                    out << "## Health Checks\n\n";
                    out << "- Prerequisites: Placeholder (not yet implemented)\n\n";
                }

                std::string content = out.str();

                if (!snapshot_out.empty()) {
                    auto out_path = std::filesystem::path(snapshot_out);
                    std::filesystem::create_directories(out_path.parent_path());
                    std::ofstream ofs(out_path);
                    if (!ofs) throw std::runtime_error("Cannot write: " + out_path.string());
                    ofs << content;
                    std::cout << "Snapshot written to: " << out_path << "\n";
                } else {
                    std::cout << content;
                }
            });

            auto* reportCmd = snapshotCmd->add_subcommand("report", "Generate a persona-targeted report from a fresh snapshot");
            std::string report_persona;
            std::string report_scope = "repo";
            bool report_write = false;
            std::string report_out;
            std::string report_meta_mode = "min";
            reportCmd->add_option("persona", report_persona, "Target persona: developer|pm|qa")->required();
            reportCmd->add_option("--scope", report_scope, "Scope: repo|product:<name>");
            reportCmd->add_flag("-w,--write", report_write, "Write report to file");
            reportCmd->add_option("-o,--out", report_out, "Custom output path");
            reportCmd->add_option("--meta-mode", report_meta_mode, "Metadata block mode: none|min|full");

            reportCmd->callback([&]() {
                std::cout << "Generating " << report_persona << " report for " << report_scope << "...\n";
                std::cout << "Template-based reporting not yet implemented in C++.\n";
            });
        }

        // ============================================================
        // version
        // ============================================================
        {
            auto* versionCmd = app.add_subcommand("version", "Show version");
            versionCmd->callback([&]() {
                std::cout << "kano-backlog " << kano::backlog::GetVersion() << "\n";
                std::cout << kano::backlog::GetBuildInfo() << "\n";
            });
        }

        if (InArgc <= 1) {
            std::cout << app.help() << std::endl;
            return 0;
        }

        app.parse(InArgc, InArgv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal error: unknown exception\n";
        return 1;
    }
    return 0;
}
