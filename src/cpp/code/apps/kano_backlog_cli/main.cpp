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
#include <iostream>
#include <string>
#include <filesystem>

using namespace kano::backlog_core;
using namespace kano::backlog_ops;

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
            std::string ref, state_str, update_agent, update_msg;
            updateStateCmd->add_option("ref", ref, "Item ID or UID")->required();
            updateStateCmd->add_option("state", state_str, "New state (new, proposed, accepted, inprogress, inreview, done, blocked, trash)")->required();
            updateStateCmd->add_option("--agent", update_agent, "Agent ID")->required();
            updateStateCmd->add_option("-m,--message", update_msg, "Optional log message");
            bool update_force = false;
            updateStateCmd->add_flag("-f,--force", update_force, "Bypass Ready gate validation");

            updateStateCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");

                auto state_opt = parse_item_state(state_str);
                if (!state_opt) throw std::runtime_error("Invalid item state: " + state_str);

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
            std::string dec_ref, dec_text, dec_agent, dec_source;
            decisionCmd->add_option("ref", dec_ref, "Item ID or UID")->required();
            decisionCmd->add_option("text", dec_text, "Decision text")->required();
            decisionCmd->add_option("--agent", dec_agent, "Agent ID")->required();
            decisionCmd->add_option("--source", dec_source, "Source of decision (e.g. meeting, email)");
            decisionCmd->callback([&]() {
                auto ctx = resolve_ctx();
                BacklogIndex index(ctx.backlog_root / ".cache" / "index" / "backlog.db");
                auto result = WorkitemOps::add_decision_writeback(
                    index, ctx.product_root, dec_ref, dec_text, dec_agent,
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
        // view group
        // ============================================================
        {
            auto* viewCmd = app.add_subcommand("view", "Dashboard and view management");
            viewCmd->add_subcommand("refresh", "Refresh dashboards");
            viewCmd->add_subcommand("list", "List available views");
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
