#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.h>

#include "kano/backlog_core/process/noninteractive_errors.hpp"

namespace {

int g_command_step = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string shell_command_for(const std::filesystem::path& binary, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::string command = "cd /d \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
#else
    std::string command = "cd \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
#endif
    for (const auto& arg : args) {
        command += " \"" + arg + "\"";
    }
    return command;
}

void log_command_start(const std::vector<std::string>& args) {
    std::cout << "[cli_quick_smoke_test step " << ++g_command_step << "]";
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << std::endl;
}

void log_command_result(int rc, const std::filesystem::path& output_path = {}) {
    std::cout << "[cli_quick_smoke_test step " << g_command_step << "] exit=" << rc;
    if (!output_path.empty()) {
        std::cout << " output=" << output_path.string();
    }
    std::cout << std::endl;
}

int run_command(const std::filesystem::path& binary, const std::vector<std::string>& args) {
    log_command_start(args);
    const int rc = std::system(shell_command_for(binary, args).c_str());
    log_command_result(rc);
    return rc;
}

int run_command_capture(const std::filesystem::path& binary, const std::vector<std::string>& args, const std::filesystem::path& output_path) {
    log_command_start(args);
    std::string command = shell_command_for(binary, args);
    command += " > \"" + output_path.string() + "\" 2>&1";
    const int rc = std::system(command.c_str());
    log_command_result(rc, output_path);
    return rc;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void expect_embedded_raw_literals_within_msvc_limit(const std::filesystem::path& path) {
    constexpr std::size_t kMaxRawLiteralBytes = 16000;
    const auto source = read_text(path);
    std::size_t cursor = 0;
    std::size_t literal_count = 0;

    while (true) {
        const auto raw_start = source.find("R\"", cursor);
        if (raw_start == std::string::npos) {
            break;
        }
        const auto content_start_marker = source.find('(', raw_start + 2);
        expect(content_start_marker != std::string::npos,
            "unterminated raw literal delimiter in " + path.string());
        const auto delimiter = source.substr(
            raw_start + 2,
            content_start_marker - (raw_start + 2));
        expect(delimiter.size() <= 16,
            "invalid raw literal delimiter in " + path.string());

        const auto close_token = ")" + delimiter + "\"";
        const auto content_start = content_start_marker + 1;
        const auto content_end = source.find(close_token, content_start);
        expect(content_end != std::string::npos,
            "unterminated raw literal in " + path.string());
        const auto literal_bytes = content_end - content_start;
        expect(literal_bytes <= kMaxRawLiteralBytes,
            "embedded raw literal exceeds the 16000-byte MSVC guard in " +
                path.string() + ": " + std::to_string(literal_bytes) + " bytes");
        ++literal_count;
        cursor = content_end + close_token.size();
    }

    expect(literal_count > 0, "no embedded raw literals found in " + path.string());
}

Json::Value read_json(const std::filesystem::path& path) {
    const auto text = read_text(path);
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder, input, &root, &errors)) {
        throw std::runtime_error("failed to parse JSON " + path.string() + ": " + errors);
    }
    return root;
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

bool json_array_contains_string(const Json::Value& array, const std::string& expected) {
    if (!array.isArray()) {
        return false;
    }
    for (const auto& value : array) {
        if (value.isString() && value.asString() == expected) {
            return true;
        }
    }
    return false;
}

const Json::Value* find_json_object(
    const Json::Value& root,
    const std::string& array_key,
    const std::string& field,
    const std::string& value
) {
    const auto& array = root[array_key];
    if (!array.isArray()) {
        return nullptr;
    }
    for (const auto& entry : array) {
        if (entry.isObject() && entry[field].asString() == value) {
            return &entry;
        }
    }
    return nullptr;
}

std::string replace_frontmatter_uid(std::string content, const std::string& uid) {
    const auto frontmatter_end = content.find("\n---", 3);
    auto line_start = content.find("\nuid:");
    if (line_start == std::string::npos || frontmatter_end == std::string::npos || ++line_start >= frontmatter_end) {
        throw std::runtime_error("failed to locate frontmatter uid line");
    }
    const auto line_end = content.find('\n', line_start);
    content.replace(line_start, line_end - line_start, "uid: " + uid);
    return content;
}

std::string replace_frontmatter_scalar(
    std::string content,
    const std::string& key,
    const std::string& value
) {
    const auto frontmatter_end = content.find("\n---", 3);
    const auto marker = "\n" + key + ":";
    auto line_start = content.find(marker);
    if (line_start == std::string::npos || frontmatter_end == std::string::npos ||
        line_start >= frontmatter_end) {
        throw std::runtime_error("failed to locate frontmatter scalar: " + key);
    }
    ++line_start;
    const auto line_end = content.find('\n', line_start);
    content.replace(line_start, line_end - line_start, key + ": " + value);
    return content;
}

std::string frontmatter_uid(const std::string& content) {
    auto line_start = content.find("\nuid:");
    if (line_start == std::string::npos) {
        throw std::runtime_error("failed to locate frontmatter uid line");
    }
    line_start += 5;
    const auto value_start = content.find_first_not_of(" \t", line_start);
    const auto line_end = content.find_first_of("\r\n", value_start);
    return content.substr(value_start, line_end - value_start);
}

std::string mask_frontmatter_uid(const std::string& content) {
    return replace_frontmatter_uid(content, "<UID>");
}

void expect_command_capture_success(
    int rc,
    const std::filesystem::path& output_path,
    const std::string& message
) {
    if (rc == 0) {
        return;
    }

    std::ostringstream detail;
    detail << message << " (exit code " << rc << ")";
    if (std::filesystem::exists(output_path)) {
        detail << "\n--- command output ---\n" << read_text(output_path);
    }
    throw std::runtime_error(detail.str());
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << text;
}

std::string yaml_string_list(const std::vector<std::string>& values, int indent) {
    std::ostringstream out;
    if (values.empty()) {
        out << std::string(static_cast<std::size_t>(indent), ' ') << "[]\n";
        return out.str();
    }
    for (const auto& value : values) {
        out << std::string(static_cast<std::size_t>(indent), ' ') << "- " << value << "\n";
    }
    return out.str();
}

std::string yaml_double_quoted_value(const std::string& value) {
    std::ostringstream escaped;
    for (const auto ch : value) {
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            default: escaped << ch; break;
        }
    }
    return escaped.str();
}

std::string list_fixture_item_markdown(
    const std::string& id,
    const std::string& uid,
    const std::string& title,
    const std::string& state,
    const std::string& priority,
    const std::string& updated,
    const std::vector<std::string>& blocks = {},
    const std::vector<std::string>& blocked_by = {}
) {
    std::ostringstream out;
    out << "---\n";
    out << "id: " << id << "\n";
    out << "uid: " << uid << "\n";
    out << "type: Task\n";
    out << "title: \"" << yaml_double_quoted_value(title) << "\"\n";
    out << "state: " << state << "\n";
    out << "priority: " << priority << "\n";
    out << "parent: ~\n";
    out << "duplicate_of: ~\n";
    out << "owner: ~\n";
    out << "area: general\n";
    out << "iteration: backlog\n";
    out << "created: " << updated << "\n";
    out << "updated: " << updated << "\n";
    out << "external:\n  {}\n";
    out << "links:\n";
    out << "  relates:\n" << yaml_string_list({}, 4);
    out << "  blocks:\n" << yaml_string_list(blocks, 4);
    out << "  blocked_by:\n" << yaml_string_list(blocked_by, 4);
    out << "decisions:\n  []\n";
    out << "tags:\n  []\n";
    out << "---\n\n";
    out << "# Context\n\nList compaction fixture.\n\n";
    out << "# Goal\n\nExercise reversible list projection.\n\n";
    out << "# Approach\n\nUse deterministic canonical metadata.\n\n";
    out << "# Acceptance Criteria\n\n- Item is readable.\n\n";
    out << "# Risks / Dependencies\n\nNone.\n\n";
    out << "# Worklog\n\n";
    return out.str();
}

std::vector<std::string> with_duplicate_admission(std::vector<std::string> args, const std::string& query) {
    args.push_back("--duplicate-search-query");
    args.push_back(query);
    args.push_back("--duplicate-search-scope");
    args.push_back("quick-smoke-product");
    args.push_back("--duplicate-decision");
    args.push_back("create");
    return args;
}

std::filesystem::path find_binary(const std::filesystem::path& repo_root, const std::filesystem::path& executable_path) {
    const std::string exe_suffix =
#ifdef _WIN32
        ".exe";
#else
        "";
#endif
    const std::vector<std::filesystem::path> candidates = {
        executable_path.parent_path() / ("kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/windows-ninja-msvc/release/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/linux-ninja-clang/debug/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/linux-ninja-clang/release/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/linux-ninja-gcc/debug/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/linux-ninja-gcc/release/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/macos-ninja-clang-x64/debug/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/macos-ninja-clang-x64/release/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/macos-ninja-clang-arm64/debug/kano-backlog" + exe_suffix),
        repo_root / ("src/cpp/out/bin/macos-ninja-clang-arm64/release/kano-backlog" + exe_suffix)
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    try {
        const std::filesystem::path repo_root(KANO_REPO_ROOT);
        const std::filesystem::path executable_path =
            (argc > 0 && argv != nullptr) ? std::filesystem::absolute(argv[0]) : std::filesystem::path();
        const auto binary = find_binary(repo_root, executable_path);
        expect(std::filesystem::exists(binary), "native binary not found for cli_quick_smoke_test");
        std::filesystem::current_path(std::filesystem::exists(repo_root) ? repo_root : binary.parent_path());

        const std::vector<std::string> reactivation_dogfood_ids = {
            "KOB-TSK-0025",
            "KOB-TSK-0006",
            "KOB-BUG-0020",
            "KOB-TSK-0028",
            "KOB-TSK-0030",
            "KOB-TSK-0001",
            "KOB-TSK-0004",
        };
        const auto skill_contract = read_text(repo_root / "SKILL.md");
        const auto cli_source = read_text(repo_root / "src/cpp/code/apps/kano_backlog_cli/main.cpp");
        expect(cli_source.find("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE") != std::string::npos &&
                cli_source.find("\"pixi.exe\", argv.data(), true") != std::string::npos &&
                cli_source.find("argv_storage.front().c_str(), argv.data(), false") != std::string::npos,
            "Windows webview launch must own both Pixi and Drogon process trees in kill-on-close jobs");
        for (const auto& asset_path : std::vector<std::filesystem::path>{
                 repo_root / "src/cpp/code/apps/kano_backlog_webview/assets/backboard_app_js.hpp",
                 repo_root / "src/cpp/code/apps/kano_backlog_webview/assets/backboard_graph_inspector_js.hpp",
                 repo_root / "src/cpp/code/apps/kano_backlog_webview/assets/backboard_css.hpp",
             }) {
            expect_embedded_raw_literals_within_msvc_limit(asset_path);
        }

        expect(skill_contract.find("### Reactivation Review") != std::string::npos,
            "SKILL contract should define Reactivation Review");
        expect(skill_contract.find("### Stale Solution Check") != std::string::npos,
            "SKILL contract should define Stale Solution Check");
        expect(skill_contract.find("Bug evidence can remain valid") != std::string::npos &&
                   skill_contract.find("old proposed fix is untrusted") != std::string::npos,
            "SKILL contract should distinguish durable bug evidence from an untrusted old proposed fix");
        for (const auto* state_row : {
                 "| Proposed |",
                 "| Ready |",
                 "| InProgress |",
                 "| Review |",
                 "| Done / Post-Done |",
             }) {
            expect(skill_contract.find(state_row) != std::string::npos,
                std::string("SKILL contract should cover reactivation state row ") + state_row);
        }
        for (const auto& dogfood_id : reactivation_dogfood_ids) {
            expect(skill_contract.find(dogfood_id) != std::string::npos,
                "SKILL contract should list initial reactivation dogfood item " + dogfood_id);
        }

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();

        expect(run_command(binary, {"--help"}) == 0, "help command failed");
        expect(run_command(binary, {"--version"}) == 0, "version command failed");
        expect(run_command(binary, {"gui", "--help"}) == 0, "gui help failed");
        expect(run_command(binary, {"webview", "--help"}) == 0, "webview help failed");
        const auto webview_dry_run_output = std::filesystem::temp_directory_path() /
            ("kano-backlog-webview-dry-run-" + std::to_string(unique) + ".txt");
        expect_command_capture_success(
            run_command_capture(binary, {"webview", "--dry-run"}, webview_dry_run_output),
            webview_dry_run_output,
            "webview dry-run failed");
        const auto webview_dry_run_text = read_text(webview_dry_run_output);
        expect(webview_dry_run_text.find("Backboard launcher: native Drogon host") != std::string::npos,
            "webview dry-run should select the native Drogon host launcher");
        expect(webview_dry_run_text.find("Command: pixi run webview") != std::string::npos,
            "webview dry-run should delegate to the established all-in-one host task");
        expect(webview_dry_run_text.find("no build, server, or browser started") != std::string::npos,
            "webview dry-run should remain side-effect free");
        std::filesystem::remove(webview_dry_run_output);
        expect(run_command(binary, {"admin", "items", "--help"}) == 0, "admin items help failed");
        expect(run_command(binary, {"admin", "validate", "--help"}) == 0, "admin validate help failed");

        const auto temp_root = std::filesystem::temp_directory_path() / ("kano-backlog-cli-quick-smoke-" + std::to_string(unique));
        std::filesystem::create_directories(temp_root);
        const auto original_cwd = std::filesystem::current_path();
        std::filesystem::current_path(temp_root);

        const auto migration_plan_output = temp_root / "migration-plan-invalid-request.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "migration", "plan", "INVALID-INIT-0001",
                "--source-product", "invalid-source",
                "--target-product", "invalid-target",
                "--max-source-inventory-items", "0",
                "--compact"
            }, migration_plan_output),
            migration_plan_output,
            "migration plan CLI callback failed"
        );
        const auto migration_plan_text = read_text(migration_plan_output);
        expect(migration_plan_text.find("\"schema\":\"kob.cross_product_migration.plan.v1\"") != std::string::npos,
            "migration plan CLI callback should emit the versioned plan receipt");
        expect(migration_plan_text.find("\"max_source_inventory_items_out_of_range\"") != std::string::npos,
            "migration plan CLI callback should preserve parsed request bounds after command registration");

        const auto migration_status_output = temp_root / "migration-status-missing-journal.txt";
        const auto migration_status_rc = run_command_capture(binary, {
            "migration", "status", std::string(64, '0'),
            "--backlog-root", temp_root.string(),
            "--compact"
        }, migration_status_output);
        expect_command_capture_success(
            migration_status_rc,
            migration_status_output,
            "migration status CLI callback failed");
        const auto migration_status_text = read_text(migration_status_output);
        expect(migration_status_text.find("\"schema\":\"kob.cross_product_migration.status.v1\"") != std::string::npos &&
               migration_status_text.find("\"status\":\"unknown\"") != std::string::npos,
            "migration recovery CLI callback should retain its registered option builder");

        expect(run_command(binary, {"admin", "init", "--product", "quick-smoke-product", "--agent", "tester"}) == 0,
            "admin init command failed");
        const auto quick_product_root = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product";
        const auto quick_views_root = quick_product_root / "views";
        expect(std::filesystem::exists(quick_views_root), "admin init should retain the views scaffold");
        expect(
            !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_Active.md") &&
                !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_New.md") &&
                !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_Done.md"),
            "admin init should not generate plain Markdown dashboards");
        const auto custom_view_path = quick_views_root / "Team_Review.md";
        write_text(custom_view_path, "# Team review\n\nTracks [[QS-TSK-0001]].\n");
        const auto custom_view_list_output = temp_root / "custom-view-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "view", "list"
            }, custom_view_list_output),
            custom_view_list_output,
            "custom view list failed");
        expect(read_text(custom_view_list_output).find("Team_Review.md") != std::string::npos,
            "view list should discover hand-authored custom views");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "view", std::string("re") + "fresh"
        }) != 0, "retired generated-dashboard command should be rejected");
        expect(run_command(binary, {"-P", "quick-smoke-product", "admin", "sync-sequences"}) == 0,
            "sync-sequences failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Missing duplicate admission", "--agent", "tester"}) != 0,
            "workitem create without duplicate admission should fail");
        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Quick smoke task", "--agent", "tester", "--profile-mutations"}, "Quick smoke task")) == 0,
            "workitem create failed");
        const auto task_receipt_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "_meta" / "duplicate-admission" / "QS-TSK-0001.json";
        expect(std::filesystem::exists(task_receipt_path), "workitem create should write duplicate admission receipt");
        const auto task_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "0000" / "QS-TSK-0001_quick-smoke-task.md";

        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "decision", "QS-TSK-0001",
            "Retain positional decision text.", "--agent", "tester", "--source", "quick-smoke"
        }) == 0, "workitem decision positional text failed");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "decision", "QS-TSK-0001",
            "--decision", "Retain option decision text.", "--agent", "tester"
        }) == 0, "workitem decision option text failed");
        const auto decision_text_path = temp_root / "decision-input.txt";
        write_text(decision_text_path, "Retain file decision text.\n");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "item", "add-decision", "QS-TSK-0001",
            "--decision-file", decision_text_path.string(), "--agent", "tester"
        }) == 0, "add-decision alias file text failed");
        const auto decision_conflict_output = temp_root / "decision-conflict.txt";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "decision", "QS-TSK-0001",
            "Conflicting decision text.", "--decision-file", decision_text_path.string(), "--agent", "tester"
        }, decision_conflict_output) != 0, "decision text plus decision-file should fail");
        expect(read_text(decision_conflict_output).find(
            "Use either positional/--decision text or --decision-file, not both") != std::string::npos,
            "decision text plus decision-file should report the real conflict");
        const auto decision_task_text = read_text(task_path);
        expect(decision_task_text.find("Retain positional decision text.") != std::string::npos &&
               decision_task_text.find("Retain option decision text.") != std::string::npos &&
               decision_task_text.find("Retain file decision text.") != std::string::npos,
            "decision command variants should persist their text");

        expect(run_command(binary, {"admin", "init", "--product", "second-product", "--agent", "tester"}) == 0,
            "second product admin init failed");
        expect(run_command(binary, {"-P", "second-product", "admin", "sync-sequences"}) == 0,
            "second product sync-sequences failed");
        expect(run_command(binary, with_duplicate_admission({"-P", "second-product", "workitem", "create", "-t", "task", "--title", "Cross product target", "--agent", "tester"}, "Cross product target")) == 0,
            "second product target creation failed");

        const auto quick_product_list_output = temp_root / "quick-product-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "list", "--type", "task"
            }, quick_product_list_output),
            quick_product_list_output,
            "quick product item list failed");
        const auto quick_product_list_text = read_text(quick_product_list_output);
        expect(quick_product_list_text.find("QS-TSK-0001") != std::string::npos &&
               quick_product_list_text.find("SP-TSK-0001") == std::string::npos,
            "quick product item list should exclude second-product rows from the shared index");
        const auto repeated_quick_product_list_output = temp_root / "quick-product-list-repeat.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "list", "--type", "task"
            }, repeated_quick_product_list_output),
            repeated_quick_product_list_output,
            "repeated default product item list failed");
        expect(read_text(repeated_quick_product_list_output) == quick_product_list_text,
            "default plain item list bytes and ordering must remain deterministic");

        const auto second_product_list_output = temp_root / "second-product-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--type", "task"
            }, second_product_list_output),
            second_product_list_output,
            "second product item list failed");
        const auto second_product_list_text = read_text(second_product_list_output);
        expect(second_product_list_text.find("SP-TSK-0001") != std::string::npos &&
               second_product_list_text.find("QS-TSK-0001") == std::string::npos,
            "second product item list should exclude quick-product rows from the shared index");

        const auto empty_product_list_output = temp_root / "empty-product-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--state", "Ready"
            }, empty_product_list_output),
            empty_product_list_output,
            "empty product-scoped item list failed");
        expect(read_text(empty_product_list_output).find("No items found.") != std::string::npos,
            "empty product-scoped item list should render an explicit empty result");

        const auto second_task_path = temp_root / "_kano" / "backlog" / "products" / "second-product" / "items" / "task" / "0000" / "SP-TSK-0001_cross-product-target.md";
        const auto unrepairable_uid_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "0000" / "QS-TSK-0999_unrepairable.md";
        auto stale_second_task_text = read_text(second_task_path);
        const auto stale_state_position = stale_second_task_text.find("state: Proposed");
        const auto stale_title_position = stale_second_task_text.find("Cross product target");
        expect(stale_state_position != std::string::npos && stale_title_position != std::string::npos,
            "second product fixture should contain indexed state and title markers");
        stale_second_task_text.replace(stale_state_position, std::string("state: Proposed").size(), "state: Review");
        stale_second_task_text.replace(stale_title_position, std::string("Cross product target").size(), "Canonical cross product target");
        stale_second_task_text = replace_frontmatter_scalar(
            stale_second_task_text,
            "updated",
            "2026-07-28"
        );
        write_text(second_task_path, stale_second_task_text);

        const auto canonical_list_output = temp_root / "canonical-product-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--type", "task", "--state", "Review"
            }, canonical_list_output),
            canonical_list_output,
            "canonical product item list failed");
        const auto canonical_list_text = read_text(canonical_list_output);
        expect(canonical_list_text.find("SP-TSK-0001") != std::string::npos &&
               canonical_list_text.find("Canonical cross product target") != std::string::npos,
            "product item list should use canonical state and title when the index row is stale");

        const auto stale_state_list_output = temp_root / "stale-state-product-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--state", "Proposed"
            }, stale_state_list_output),
            stale_state_list_output,
            "stale-state product item list failed");
        expect(read_text(stale_state_list_output).find("No items found.") != std::string::npos,
            "product item list should not retain a stale indexed state after canonical change");

        const auto second_product_items =
            temp_root / "_kano" / "backlog" / "products" / "second-product" / "items" / "task" / "0000";
        write_text(
            second_product_items / "SP-TSK-0002_old-done-topic.md",
            list_fixture_item_markdown(
                "SP-TSK-0002",
                "019f2000-0002-7000-8000-000000000002",
                "Old Done topic item",
                "Done",
                "P2",
                "2025-01-04"
            )
        );
        write_text(
            second_product_items / "SP-TSK-0003_old-done-group.md",
            list_fixture_item_markdown(
                "SP-TSK-0003",
                "019f2000-0003-7000-8000-000000000003",
                "Old Done group item",
                "Done",
                "P2",
                "2025-01-03"
            )
        );
        write_text(
            second_product_items / "SP-TSK-0004_old-ready.md",
            list_fixture_item_markdown(
                "SP-TSK-0004",
                "019f2000-0004-7000-8000-000000000004",
                "Old Ready item",
                "Ready",
                "P1",
                "2025-01-02"
            )
        );
        write_text(
            second_product_items / "SP-TSK-0005_old-done-dependency.md",
            list_fixture_item_markdown(
                "SP-TSK-0005",
                "019f2000-0005-7000-8000-000000000005",
                "Old Done dependency item",
                "Done",
                "P2",
                "2025-01-01",
                {"SP-TSK-0004"}
            )
        );
        const std::string quoted_multiline_title = "Quoted \"title\"\nsecond line";
        write_text(
            second_product_items / "SP-TSK-0006_quoted-multiline-title.md",
            list_fixture_item_markdown(
                "SP-TSK-0006",
                "019f2000-0006-7000-8000-000000000006",
                quoted_multiline_title,
                "Ready",
                "P1",
                "2026-07-28"
            )
        );
        const auto write_priority_variant = [&](const std::string& filename,
                                                const std::string& id,
                                                const std::string& uid,
                                                const std::string& priority_yaml) {
            auto markdown = list_fixture_item_markdown(
                id,
                uid,
                "Priority variant " + id,
                "Done",
                "P2",
                "2025-01-01"
            );
            write_text(
                second_product_items / filename,
                replace_frontmatter_scalar(markdown, "priority", priority_yaml)
            );
        };
        write_priority_variant(
            "SP-TSK-0010_absent-priority.md",
            "SP-TSK-0010",
            "019f2000-0010-7000-8000-000000000010",
            "~"
        );
        write_priority_variant(
            "SP-TSK-0014_upper-priority.md",
            "SP-TSK-0014",
            "019f2000-0014-7000-8000-000000000014",
            "P1"
        );
        write_priority_variant(
            "SP-TSK-0015_lower-priority.md",
            "SP-TSK-0015",
            "019f2000-0015-7000-8000-000000000015",
            "p1"
        );

        const auto compaction_topic_path =
            temp_root / "_kano" / "backlog" / "topics" / "large-list" / "manifest.json";
        const auto second_task_uid = frontmatter_uid(read_text(second_task_path));
        write_text(
            compaction_topic_path,
            "{\n"
            "  \"topic\": \"large-list\",\n"
            "  \"agent\": \"tester\",\n"
            "  \"created_at\": \"2026-07-28T00:00:00Z\",\n"
            "  \"updated_at\": \"2026-07-28T00:00:00Z\",\n"
            "  \"status\": \"open\",\n"
            "  \"closed_at\": null,\n"
            "  \"seed_items\": [\"" + second_task_uid +
                "\", \"019f2000-0002-7000-8000-000000000002\"],\n"
            "  \"pinned_docs\": [],\n"
            "  \"snippet_refs\": [],\n"
            "  \"related_topics\": []\n"
            "}\n"
        );
        write_text(
            temp_root / "_kano" / "backlog" / "topics" / "release-list" / "manifest.json",
            "{\n"
            "  \"topic\": \"release-list\",\n"
            "  \"agent\": \"tester\",\n"
            "  \"created_at\": \"2026-07-28T00:00:00Z\",\n"
            "  \"updated_at\": \"2026-07-28T00:00:00Z\",\n"
            "  \"status\": \"open\",\n"
            "  \"closed_at\": null,\n"
            "  \"seed_items\": [\"" + second_task_uid + "\"],\n"
            "  \"pinned_docs\": [],\n"
            "  \"snippet_refs\": [],\n"
            "  \"related_topics\": []\n"
            "}\n"
        );

        const auto compact_list_output = temp_root / "compact-product-list.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact", "--format", "json"
            }, compact_list_output),
            compact_list_output,
            "compact product item list failed");
        const auto compact_list_text = read_text(compact_list_output);
        const auto compact_list_json = read_json(compact_list_output);
        expect(compact_list_json["schema"].asString() == "kob.workitem.list.compact.v1",
            "compact list should emit a versioned JSON contract");
        expect(compact_list_json["product"].asString() == "second-product" &&
                   compact_list_json["authority"].asString() == "navigation_only" &&
                   !compact_list_json["mutates_backlog"].asBool() &&
                   compact_list_json["retrieval_consistency"].asString() ==
                       "current_canonical_state",
            "compact list should declare canonical product and read-only navigation authority");
        expect(compact_list_json["retrieval"]["operation"].asString() == "workitem.list" &&
                   compact_list_json["retrieval"]["product"].asString() == "second-product" &&
                   compact_list_text.find("kob workitem list") == std::string::npos,
            "compact list should expose structured retrieval without raw shell commands");
        expect(find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0001") != nullptr &&
                   find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0004") != nullptr,
            "compact list should keep Review and Ready items visible");
        const auto* multi_topic_item =
            find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0001");
        expect(multi_topic_item != nullptr &&
                   (*multi_topic_item)["topics"].size() == 2 &&
                   (*multi_topic_item)["topics"][0].asString() == "large-list" &&
                   (*multi_topic_item)["topics"][1].asString() == "release-list",
            "compact list should preserve complete multi-topic membership and grouping");
        bool found_multi_topic_group = false;
        for (const auto& group : compact_list_json["groups"]) {
            if (group["group_id"].asString().find(
                    "topics:2:large-list+release-list/"
                ) != std::string::npos) {
                found_multi_topic_group = true;
                expect(group["topics"].size() == 2,
                    "multi-topic compact group should retain both topic values");
            }
        }
        expect(found_multi_topic_group,
            "compact list should encode the complete topic set in group identity");
        const auto* dependency_item =
            find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0005");
        expect(dependency_item != nullptr &&
                   (*dependency_item)["dependencies"]["blocks"].size() == 1 &&
                   (*dependency_item)["dependencies"]["blocks"][0].asString() == "SP-TSK-0004",
            "compact list should keep dependency-bearing Done items and their edges visible");
        const auto* quoted_item =
            find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0006");
        expect(quoted_item != nullptr &&
                   (*quoted_item)["title"].asString() == quoted_multiline_title,
            "compact JSON should round-trip quoted and newline-containing titles");
        expect(find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0002") == nullptr &&
                   find_json_object(compact_list_json, "items", "item_id", "SP-TSK-0003") == nullptr,
            "compact list should omit old Done items from the shown item payload");
        const std::string compact_group_id =
            "state:done/type:task/priority:1:P2/topics:0/updated:before-2026-06-28";
        const auto* compact_group = find_json_object(
            compact_list_json, "groups", "group_id", compact_group_id
        );
        expect(compact_group != nullptr &&
                   (*compact_group)["priority"].asString() == "P2",
            "compact list should publish a stable omitted group ID and exact priority");
        const auto priority_group_suffix = "/topics:0/updated:before-2026-06-28";
        const auto* absent_priority_group = find_json_object(
            compact_list_json,
            "groups",
            "group_id",
            "state:done/type:task/priority:0" + std::string(priority_group_suffix)
        );
        const auto* upper_priority_group = find_json_object(
            compact_list_json,
            "groups",
            "group_id",
            "state:done/type:task/priority:1:P1" + std::string(priority_group_suffix)
        );
        const auto* lower_priority_group = find_json_object(
            compact_list_json,
            "groups",
            "group_id",
            "state:done/type:task/priority:1:p1" + std::string(priority_group_suffix)
        );
        expect(absent_priority_group != nullptr &&
                   (*absent_priority_group)["priority"].isNull() &&
                   upper_priority_group != nullptr &&
                   (*upper_priority_group)["priority"].asString() == "P1" &&
                   lower_priority_group != nullptr &&
                   (*lower_priority_group)["priority"].asString() == "p1",
            "compact JSON must preserve injective absent, empty, and case-sensitive priorities");
        expect(compact_list_json["recency"]["cutoff_source"].asString() ==
                   "latest_updated_window" &&
                   compact_list_json["recency"]["recent_days"].asInt() == 30 &&
                   compact_list_json["recency"]["replay_cutoff"].isNull(),
            "default compact JSON should describe its current-anchor recency window");

        const auto exact_compact_output = temp_root / "compact-exact-item.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact",
                "--item", "SP-TSK-0002", "--format", "json"
            }, exact_compact_output),
            exact_compact_output,
            "compact exact item retrieval failed");
        const auto exact_compact_json = read_json(exact_compact_output);
        expect(find_json_object(
                   exact_compact_json, "items", "item_id", "SP-TSK-0002"
               ) != nullptr &&
                   exact_compact_json["selection"]["item"].asString() == "SP-TSK-0002",
            "exact item retrieval should recover an omitted item");

        const auto state_compact_output = temp_root / "compact-done-state.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact",
                "--state", "Done", "--format", "json"
            }, state_compact_output),
            state_compact_output,
            "compact state retrieval failed");
        const auto state_compact_json = read_json(state_compact_output);
        expect(find_json_object(state_compact_json, "items", "item_id", "SP-TSK-0002") != nullptr &&
                   find_json_object(state_compact_json, "items", "item_id", "SP-TSK-0003") != nullptr &&
                   find_json_object(state_compact_json, "items", "item_id", "SP-TSK-0005") != nullptr,
            "state retrieval should recover every matching omitted and visible Done item");

        const auto topic_compact_output = temp_root / "compact-topic.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact",
                "--topic", "large-list", "--format", "json"
            }, topic_compact_output),
            topic_compact_output,
            "compact topic retrieval failed");
        const auto topic_compact_json = read_json(topic_compact_output);
        expect(find_json_object(topic_compact_json, "items", "item_id", "SP-TSK-0001") != nullptr &&
                   find_json_object(topic_compact_json, "items", "item_id", "SP-TSK-0002") != nullptr &&
                   topic_compact_json["selection"]["topic"].asString() == "large-list",
            "topic retrieval should recover visible and omitted topic members");

        const auto group_compact_output = temp_root / "compact-group.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact",
                "--group", compact_group_id, "--format", "json"
            }, group_compact_output),
            group_compact_output,
            "compact group retrieval failed");
        const auto group_compact_json = read_json(group_compact_output);
        expect(find_json_object(group_compact_json, "items", "item_id", "SP-TSK-0003") != nullptr &&
                   find_json_object(group_compact_json, "items", "item_id", "SP-TSK-0005") != nullptr,
            "group retrieval should recover omitted and dependency-visible group members");
        expect(group_compact_json["recency"]["cutoff_source"].asString() ==
                   "group_selector" &&
                   group_compact_json["recency"]["recent_days"].isNull() &&
                   group_compact_json["recency"]["replay_cutoff"].asString() ==
                       "2026-06-28",
            "group retrieval JSON should identify and preserve its replay cutoff");

        const auto canonical_json_output = temp_root / "canonical-product-list.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--format", "json"
            }, canonical_json_output),
            canonical_json_output,
            "canonical JSON item list failed");
        const auto canonical_json = read_json(canonical_json_output);
        const auto* canonical_quoted_item =
            find_json_object(canonical_json, "items", "item_id", "SP-TSK-0006");
        expect(canonical_json["schema"].asString() == "kob.workitem.list.v1" &&
                   find_json_object(canonical_json, "items", "item_id", "SP-TSK-0002") != nullptr &&
                   find_json_object(canonical_json, "items", "item_id", "SP-TSK-0003") != nullptr &&
                   canonical_quoted_item != nullptr &&
                   (*canonical_quoted_item)["title"].asString() == quoted_multiline_title,
            "canonical JSON list should remain complete and un-compacted");

        const auto default_invalid_state_output = temp_root / "default-invalid-state-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--state", "NotAState"
            }, default_invalid_state_output),
            default_invalid_state_output,
            "legacy default plain invalid-state behavior changed");
        expect(read_text(default_invalid_state_output).find("SP-TSK-0001") != std::string::npos,
            "default plain list should retain its existing silent invalid-filter behavior");

        const auto plain_item_invalid_state_output =
            temp_root / "plain-item-invalid-state-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list",
                "--item", "SP-TSK-0002", "--state", "NotAState"
            }, plain_item_invalid_state_output),
            plain_item_invalid_state_output,
            "plain exact-item invalid-state behavior changed");
        expect(read_text(plain_item_invalid_state_output).find("SP-TSK-0002") != std::string::npos,
            "plain exact-item retrieval should silently ignore an invalid legacy state filter");

        const auto plain_topic_invalid_type_output =
            temp_root / "plain-topic-invalid-type-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list",
                "--topic", "large-list", "--type", "NotAType"
            }, plain_topic_invalid_type_output),
            plain_topic_invalid_type_output,
            "plain topic invalid-type behavior changed");
        expect(read_text(plain_topic_invalid_type_output).find("SP-TSK-0002") != std::string::npos,
            "plain topic retrieval should silently ignore an invalid legacy type filter");

        const auto compact_invalid_state_output = temp_root / "compact-invalid-state-list.txt";
        expect(run_command_capture(binary, {
            "-P", "second-product", "workitem", "list", "--compact",
            "--state", "NotAState", "--format", "json"
        }, compact_invalid_state_output) != 0,
            "compact invalid state should fail closed");
        expect(read_text(compact_invalid_state_output).find("Invalid item state: NotAState") != std::string::npos,
            "compact invalid state should report the rejected selector");

        const auto json_invalid_type_output = temp_root / "json-invalid-type-list.txt";
        expect(run_command_capture(binary, {
            "-P", "second-product", "workitem", "list",
            "--type", "NotAType", "--format", "json"
        }, json_invalid_type_output) != 0,
            "JSON invalid type should fail closed");
        expect(read_text(json_invalid_type_output).find("Invalid item type: NotAType") != std::string::npos,
            "JSON invalid type should report the rejected selector");

        write_text(
            second_product_items / "SP-TSK-0099_ambiguous-one.md",
            list_fixture_item_markdown(
                "SP-TSK-0099",
                "019f2000-0099-7000-8000-000000000001",
                "Ambiguous display ID one",
                "Ready",
                "P1",
                "2026-07-28"
            )
        );
        write_text(
            second_product_items / "SP-TSK-0099_ambiguous-two.md",
            list_fixture_item_markdown(
                "SP-TSK-0099",
                "019f2000-0099-7000-8000-000000000002",
                "Ambiguous display ID two",
                "Done",
                "P1",
                "2026-07-28"
            )
        );
        const auto ambiguous_item_output = temp_root / "compact-ambiguous-item.txt";
        expect(run_command_capture(binary, {
            "-P", "second-product", "workitem", "list", "--compact",
            "--item", "SP-TSK-0099", "--state", "Ready", "--format", "json"
        }, ambiguous_item_output) != 0,
            "ambiguous compact display ID should fail closed before state filtering");
        expect(read_text(ambiguous_item_output).find(
                   "Ambiguous item reference: SP-TSK-0099; use UID"
               ) != std::string::npos,
            "ambiguous compact display ID should require UID retrieval");

        const auto exact_uid_output = temp_root / "compact-exact-uid.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "second-product", "workitem", "list", "--compact",
                "--item", "019f2000-0099-7000-8000-000000000001", "--format", "json"
            }, exact_uid_output),
            exact_uid_output,
            "exact UID retrieval failed after display-ID ambiguity");
        const auto exact_uid_text = read_text(exact_uid_output);
        expect(exact_uid_text.find("Ambiguous display ID one") != std::string::npos &&
                   exact_uid_text.find("Ambiguous display ID two") == std::string::npos,
            "exact UID retrieval should return one unambiguous item");

        for (const auto& fixture_name : {
                 "SP-TSK-0002_old-done-topic.md",
                 "SP-TSK-0003_old-done-group.md",
                 "SP-TSK-0004_old-ready.md",
                 "SP-TSK-0005_old-done-dependency.md",
                 "SP-TSK-0006_quoted-multiline-title.md",
                 "SP-TSK-0010_absent-priority.md",
                 "SP-TSK-0014_upper-priority.md",
                 "SP-TSK-0015_lower-priority.md",
                 "SP-TSK-0099_ambiguous-one.md",
                 "SP-TSK-0099_ambiguous-two.md"
             }) {
            std::filesystem::remove(second_product_items / fixture_name);
        }

        write_text(unrepairable_uid_path, "not frontmatter\n");
        const auto uid_unrepairable_output = temp_root / "validate-uids-unrepairable.txt";
        expect(run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product", "--fix"}, uid_unrepairable_output) != 0,
            "UID fix should fail closed when an item cannot be parsed or repaired");
        expect(read_text(uid_unrepairable_output).find("UID repair failed for 1 item(s)") != std::string::npos,
            "UID fix should report its unrepaired item count");
        std::filesystem::remove(unrepairable_uid_path);

        const auto valid_task_text = read_text(task_path);
        const auto invalid_task_text = replace_frontmatter_uid(valid_task_text, "55aeac65-d3d5-4805-894b-b8adcb73a69e");
        write_text(task_path, invalid_task_text);

        const auto uid_invalid_output = temp_root / "validate-uids-invalid.txt";
        expect(run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product"}, uid_invalid_output) != 0,
            "malformed item UID should fail validation");
        expect(read_text(uid_invalid_output).find("FAIL QS-TSK-0001: malformed UID") != std::string::npos,
            "malformed item UID should produce a bounded diagnostic");

        const auto uid_fix_dry_run_output = temp_root / "validate-uids-fix-dry-run.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product", "--fix"}, uid_fix_dry_run_output),
            uid_fix_dry_run_output,
            "UID fix dry-run failed");
        expect(read_text(uid_fix_dry_run_output).find("DRY-RUN QS-TSK-0001") != std::string::npos,
            "UID fix should default to dry-run");
        expect(read_text(task_path) == invalid_task_text, "UID fix dry-run must not mutate the item");

        const auto uid_fix_no_agent_output = temp_root / "validate-uids-fix-no-agent.txt";
        expect(run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product", "--fix", "--apply"}, uid_fix_no_agent_output) != 0,
            "UID fix apply without agent should fail");
        expect(read_text(uid_fix_no_agent_output).find("--agent is required") != std::string::npos,
            "UID fix apply should explain its agent requirement");

        const auto uid_fix_apply_output = temp_root / "validate-uids-fix-apply.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product", "--fix", "--apply", "--agent", "tester"}, uid_fix_apply_output),
            uid_fix_apply_output,
            "UID fix apply failed");
        const auto repaired_task_text = read_text(task_path);
        expect(frontmatter_uid(repaired_task_text).size() == 36 && frontmatter_uid(repaired_task_text)[14] == '7',
            "UID fix should write UUIDv7");
        expect(mask_frontmatter_uid(repaired_task_text) == mask_frontmatter_uid(invalid_task_text),
            "UID fix should change only the uid frontmatter line");

        write_text(second_task_path, replace_frontmatter_uid(read_text(second_task_path), frontmatter_uid(repaired_task_text)));
        const auto uid_duplicate_output = temp_root / "validate-uids-duplicate.txt";
        expect(run_command_capture(binary, {"validate", "uids"}, uid_duplicate_output) != 0,
            "duplicate item UIDs across products should fail validation");
        expect(read_text(uid_duplicate_output).find("duplicate UID first used by") != std::string::npos,
            "duplicate item UID should identify its first owner");
        const auto uid_duplicate_fix_output = temp_root / "validate-uids-duplicate-fix.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "uids", "--fix", "--apply", "--agent", "tester"}, uid_duplicate_fix_output),
            uid_duplicate_fix_output,
            "duplicate UID repair failed");
        expect(frontmatter_uid(read_text(task_path)) != frontmatter_uid(read_text(second_task_path)),
            "duplicate UID repair should restore global uniqueness");

        const auto uid_product_output = temp_root / "validate-uids-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "uids", "--product", "quick-smoke-product"}, uid_product_output),
            uid_product_output,
            "product UID validation should discover the shared backlog root");
        const auto uid_product_text = read_text(uid_product_output);
        expect(uid_product_text.find("OK quick-smoke-product: all 1 items have UUIDv7 UIDs") != std::string::npos &&
               uid_product_text.find("Items checked: 1") != std::string::npos,
            "product UID validation should inspect the requested product without an explicit backlog root");

        const auto uid_all_output = temp_root / "validate-uids-all.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "uids"}, uid_all_output),
            uid_all_output,
            "all-product UID validation should discover the shared backlog root");
        const auto uid_all_text = read_text(uid_all_output);
        expect(uid_all_text.find("OK quick-smoke-product: all 1 items have UUIDv7 UIDs") != std::string::npos &&
               uid_all_text.find("Items checked: 2") != std::string::npos,
            "all-product UID validation should inspect a nonzero total across configured products");

        const auto schema_check_output = temp_root / "schema-check-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"schema", "check", "--product", "quick-smoke-product"}, schema_check_output),
            schema_check_output,
            "product schema check should discover the shared backlog root");
        expect(read_text(schema_check_output).find("Total: 1 items checked, 0 with issues") != std::string::npos,
            "product schema check should inspect the requested product without an explicit backlog root");

        const auto global_schema_check_output = temp_root / "global-schema-check-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "schema", "check"
            }, global_schema_check_output),
            global_schema_check_output,
            "native schema check should accept leading global options");
        const auto admin_schema_check_output = temp_root / "admin-schema-check-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "admin", "schema", "check"
            }, admin_schema_check_output),
            admin_schema_check_output,
            "admin schema compatibility route should accept leading global options");
        expect(read_text(admin_schema_check_output) == read_text(global_schema_check_output),
            "admin schema compatibility route should preserve native schema check semantics");

        const auto schema_fix_output = temp_root / "schema-fix-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"schema", "fix", "--product", "quick-smoke-product", "--agent", "tester"}, schema_fix_output),
            schema_fix_output,
            "product schema fix dry-run should discover the shared backlog root");
        expect(read_text(schema_fix_output).find("Total: 1 checked, 1 issues, 0 fixed") != std::string::npos,
            "product schema fix dry-run should inspect the requested product without applying changes");

        const auto validate_links_output = temp_root / "validate-links-product.txt";
        write_text(
            temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "decisions" / "ADR-0001.md",
            "---\nuid: 019cdf6a-0000-7000-8000-000000000101\ntype: adr\nstatus: Accepted\n---\n\n# ADR-0001\n"
        );
        const auto link_fixture_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "9000" / "QS-TSK-9000_link-validator-fixture.md";
        const std::string valid_link_fixture =
            "---\n"
            "id: QS-TSK-9000\n"
            "uid: 019cdf6a-0000-7000-8000-000000009000\n"
            "type: Task\n"
            "title: Link validator fixture\n"
            "state: Proposed\n"
            "priority: P2\n"
            "parent: ~\n"
            "created: 2026-07-21\n"
            "updated: 2026-07-21\n"
            "area: general\n"
            "iteration: backlog\n"
            "external: {}\n"
            "links:\n"
            "  relates:\n"
            "    - SP-TSK-0001\n"
            "  blocks: []\n"
            "  blocked_by: []\n"
            "decisions:\n"
            "  - \"Evidence sentence with source/path prose (source: implementation/preflight).\"\n"
            "  - \"Cross-product target SP-TSK-0001 remains canonical in prose.\"\n"
            "tags: []\n"
            "---\n\n"
            "# Context\n\nValidate a canonical cross-product reference and ADR-0001. External thread "
            "019cdf6a-0000-7000-8000-000000009099 is provenance only. Historical identities "
            "QS-TSK-9998 and QS-TSK-9999 belong to current item QS-TSK-9000.\n\n"
            "# Goal\n\nKeep prose from becoming a whole reference.\n\n"
            "# Worklog\n\n"
            "2026-07-21 00:01 [agent=tester] Remapped ID: QS-TSK-9998 -> QS-TSK-9999\n"
            "2026-07-21 00:02 [agent=tester] Remapped ID: QS-TSK-9999 -> QS-TSK-9000\n";
        write_text(link_fixture_path, valid_link_fixture);
        expect_command_capture_success(
            run_command_capture(binary, {"validate", "links", "--product", "quick-smoke-product"}, validate_links_output),
            validate_links_output,
            "product link validation should discover the shared backlog root");
        const auto validate_links_text = read_text(validate_links_output);
        expect(validate_links_text.find("OK quick-smoke-product: 2 files, no broken links") != std::string::npos,
            "product link validation should resolve cross-product refs without an explicit backlog root");
        expect(validate_links_text.find("Evidence sentence") == std::string::npos,
            "product link validation should not treat decision prose as a whole reference");

        const auto global_validate_uids_output = temp_root / "global-validate-uids-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "validate", "uids"
            }, global_validate_uids_output),
            global_validate_uids_output,
            "native UID validation should accept leading global options");
        const auto admin_validate_uids_output = temp_root / "admin-validate-uids-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "admin", "validate", "uids"
            }, admin_validate_uids_output),
            admin_validate_uids_output,
            "admin validate UID compatibility route should accept leading global options");
        expect(read_text(admin_validate_uids_output) == read_text(global_validate_uids_output),
            "admin validate UID compatibility route should preserve native UID validation semantics");

        const auto global_validate_links_output = temp_root / "global-validate-links-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "validate", "links"
            }, global_validate_links_output),
            global_validate_links_output,
            "native link validation should accept leading global options");
        const auto admin_validate_links_output = temp_root / "admin-validate-links-product.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", temp_root.string(), "-P", "quick-smoke-product",
                "admin", "validate", "links"
            }, admin_validate_links_output),
            admin_validate_links_output,
            "admin validate links compatibility route should accept leading global options");
        expect(read_text(admin_validate_links_output) == read_text(global_validate_links_output),
            "admin validate links compatibility route should preserve native link validation semantics");

        auto structured_alias_fixture = valid_link_fixture;
        const std::string structured_alias_anchor = "    - SP-TSK-0001\n";
        const auto structured_alias_position = structured_alias_fixture.find(structured_alias_anchor);
        expect(structured_alias_position != std::string::npos,
            "link fixture should expose a structured relates insertion point");
        structured_alias_fixture.insert(
            structured_alias_position + structured_alias_anchor.size(),
            "    - QS-TSK-9998\n"
        );
        write_text(link_fixture_path, structured_alias_fixture);
        const auto validate_structured_alias_output = temp_root / "validate-links-structured-alias.txt";
        expect(run_command_capture(binary, {
            "validate", "links", "--product", "quick-smoke-product"
        }, validate_structured_alias_output) != 0,
            "a structured historical alias should remain fail-closed");
        expect(read_text(validate_structured_alias_output).find("unresolvable ref: QS-TSK-9998") != std::string::npos,
            "structured links should remain unconditional despite prose remap evidence");
        write_text(link_fixture_path, valid_link_fixture);

        write_text(link_fixture_path, valid_link_fixture + "\n# Risks / Dependencies\n\nMissing canonical dependency QS-BUG-9999.\n");
        const auto validate_missing_link_output = temp_root / "validate-links-missing.txt";
        expect(run_command_capture(binary, {"validate", "links", "--product", "quick-smoke-product"}, validate_missing_link_output) != 0,
            "missing canonical cross-product ref should fail validation");
        expect(read_text(validate_missing_link_output).find("unresolvable ref: QS-BUG-9999") != std::string::npos,
            "missing canonical ref should remain fail-closed");
        write_text(link_fixture_path, valid_link_fixture);

        const auto ambiguous_link_peer_path = second_product_items / "SP-TSK-0001_ambiguous-link-peer.md";
        write_text(
            ambiguous_link_peer_path,
            list_fixture_item_markdown(
                "SP-TSK-0001",
                "019f2000-0001-7000-8000-000000000099",
                "Ambiguous link target peer",
                "Ready",
                "P1",
                "2026-07-21"
            )
        );
        const auto validate_ambiguous_link_output = temp_root / "validate-links-ambiguous.txt";
        expect(run_command_capture(binary, {
            "validate", "links", "--product", "quick-smoke-product"
        }, validate_ambiguous_link_output) != 0,
            "an ambiguous structured display ID should remain fail-closed");
        expect(read_text(validate_ambiguous_link_output).find("unresolvable ref: SP-TSK-0001") != std::string::npos,
            "ambiguous structured links should retain the validator diagnostic");
        std::filesystem::remove(ambiguous_link_peer_path);

        const auto text_root = temp_root / "ready-fields";
        write_text(text_root / "context.md", "Quick smoke context.\n");
        write_text(text_root / "goal.md", "Quick smoke goal.\n");
        write_text(text_root / "non-goals.md", "Quick smoke non-goal.\n");
        write_text(text_root / "approach.md", "Quick smoke approach.\n");
        write_text(text_root / "intent-amendments.md", "2026-06-20: Quick smoke amendment.\n");
        write_text(text_root / "acceptance.md", "Quick smoke acceptance.\n");
        write_text(text_root / "risks.md", "Quick smoke risks.\n");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-TSK-0001",
            "--context-file", (text_root / "context.md").string(),
            "--goal-file", (text_root / "goal.md").string(),
            "--non-goals-file", (text_root / "non-goals.md").string(),
            "--approach-file", (text_root / "approach.md").string(),
            "--intent-amendments-file", (text_root / "intent-amendments.md").string(),
            "--acceptance-criteria-file", (text_root / "acceptance.md").string(),
            "--risks-file", (text_root / "risks.md").string(),
            "--agent", "tester"
        }) == 0, "set-ready failed");
        const auto task_text = read_text(task_path);
        expect(task_text.find("# Non-Goals / Do Not") != std::string::npos, "set-ready task should render Non-Goals / Do Not heading");
        expect(task_text.find("Quick smoke non-goal.") != std::string::npos, "set-ready task should persist non-goals text");
        expect(task_text.find("# Intent Amendments") != std::string::npos, "set-ready task should render Intent Amendments heading");
        expect(task_text.find("2026-06-20: Quick smoke amendment.") != std::string::npos, "set-ready task should persist intent amendments text");
        expect(task_text.find("Duplicate admission:") != std::string::npos, "create should record duplicate admission worklog evidence");

        const auto ready_transition_output = temp_root / "transition-ready.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "Ready", "--agent", "tester"
            }, ready_transition_output),
            ready_transition_output,
            "ready transition diagnostics failed"
        );
        const auto ready_transition_text = read_text(ready_transition_output);
        expect(ready_transition_text.find("Updated QS-TSK-0001: Proposed -> Ready") != std::string::npos, "ready transition should preserve update line");
        expect(ready_transition_text.find("Intent warning: Proposed->Ready intent readiness: missing parent intent") != std::string::npos,
            "ready transition should warn about missing parent intent");

        const auto inprogress_transition_output = temp_root / "transition-inprogress.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "InProgress", "--agent", "tester"
            }, inprogress_transition_output),
            inprogress_transition_output,
            "inprogress transition diagnostics failed"
        );
        const auto inprogress_transition_text = read_text(inprogress_transition_output);
        expect(inprogress_transition_text.find("Intent warning: Ready->InProgress intent preflight") != std::string::npos,
            "inprogress transition should warn about intent preflight");
        expect(inprogress_transition_text.find("no parent intent resolved") != std::string::npos,
            "inprogress transition should warn about unresolved parent intent");

        const auto review_transition_output = temp_root / "transition-review.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "Review", "--agent", "tester"
            }, review_transition_output),
            review_transition_output,
            "review transition diagnostics failed"
        );
        expect(read_text(review_transition_output).find("Intent warning: InProgress->Review intent compliance") != std::string::npos,
            "review transition should warn about missing compliance evidence");

        const auto invalid_review_start_output = temp_root / "invalid-review-start.txt";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "state", "transition", "QS-TSK-0001", "start",
            "--agent", "reviewer", "--message", "Resume review work."
        }, invalid_review_start_output) != 0, "explicit Review plus start should remain invalid");
        expect(read_text(invalid_review_start_output).find("reopen") != std::string::npos,
            "invalid Review plus start should recommend reopen");

        const auto missing_reopen_rationale_output = temp_root / "missing-reopen-rationale.txt";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001",
            "--state", "InProgress", "--agent", "reviewer"
        }, missing_reopen_rationale_output) != 0, "generic Review to InProgress should require rationale");
        expect(read_text(missing_reopen_rationale_output).find("rationale") != std::string::npos,
            "missing target-state reopen rationale should be actionable");

        const auto forced_missing_reopen_rationale_output = temp_root / "forced-missing-reopen-rationale.txt";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001",
            "--state", "InProgress", "--agent", "reviewer", "--force"
        }, forced_missing_reopen_rationale_output) != 0, "force should not bypass generic reopen rationale");
        expect(read_text(forced_missing_reopen_rationale_output).find("rationale") != std::string::npos,
            "forced missing reopen rationale should fail with the same audit guidance");

        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001",
            "--state", "InProgress", "--agent", "reviewer", "--message", "Acceptance criteria remain unmet through generic update."
        }) == 0, "generic audited reopen transition failed");
        const auto generically_reopened_task_text = read_text(task_path);
        expect(generically_reopened_task_text.find("state: InProgress") != std::string::npos,
            "generic audited reopen should restore InProgress state");
        expect(generically_reopened_task_text.find("State: Review -> InProgress: Acceptance criteria remain unmet through generic update.") != std::string::npos,
            "generic audited reopen should append actor and rationale evidence");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "Review", "--agent", "tester"
        }) == 0, "review transition after generic reopen failed");

        expect(run_command(binary, {
            "-P", "quick-smoke-product", "state", "transition", "QS-TSK-0001", "reopen",
            "--agent", "reviewer", "--message", "Acceptance criteria remain unmet."
        }) == 0, "explicit reopen transition failed");
        const auto reopened_task_text = read_text(task_path);
        expect(reopened_task_text.find("state: InProgress") != std::string::npos,
            "reopen should restore InProgress state");
        expect(reopened_task_text.find("State: Review -> InProgress: Acceptance criteria remain unmet.") != std::string::npos,
            "reopen should append audited rationale");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "Review", "--agent", "tester"
        }) == 0, "review transition after reopen failed");

        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0001",
            "--correction", "Unresolved violation blocks Done.",
            "--reason", "Drift finding remains unresolved.",
            "--applies-to", "Do Not Compliance",
            "--agent", "tester"
        }) == 0, "review drift amendment failed");
        const auto done_transition_output = temp_root / "transition-done.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0001", "--state", "Done", "--agent", "tester"
            }, done_transition_output),
            done_transition_output,
            "done transition diagnostics failed"
        );
        expect(read_text(done_transition_output).find("Intent warning: Review->Done intent alignment") != std::string::npos,
            "done transition should warn about unresolved drift evidence");

        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "item", "create", "-t", "issue", "--title", "Quick smoke issue", "--agent", "tester"}, "Quick smoke issue")) == 0,
            "workitem create issue failed");
        const auto issue_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "issue" / "0000" / "QS-ISS-0001_quick-smoke-issue.md";
        expect(std::filesystem::exists(issue_path), "workitem create did not create expected issue file");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-ISS-0001",
            "--context", "Quick smoke issue context.",
            "--goal", "Quick smoke issue goal.",
            "--do-not", "Quick smoke issue non-goal.",
            "--approach", "Quick smoke issue approach.",
            "--intent-amendments", "2026-06-20: Quick smoke issue amendment.",
            "--acceptance-criteria", "Quick smoke issue acceptance.",
            "--risks", "Quick smoke issue risks.",
            "--agent", "tester"
        }) == 0, "issue set-ready failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "check-ready", "QS-ISS-0001"}) == 0,
            "issue check-ready failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "worklog", "append", "QS-ISS-0001", "Issue worklog smoke", "--agent", "tester"}) == 0,
            "issue worklog append failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "update-state", "QS-ISS-0001", "--state", "InProgress", "--agent", "tester"}) == 0,
            "issue update-state failed");
        const auto issue_list_output = temp_root / "issue-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "list", "--type", "issue"
            }, issue_list_output),
            issue_list_output,
            "issue list failed"
        );
        expect(read_text(issue_list_output).find("QS-ISS-0001") != std::string::npos,
            "issue list did not include created issue");
        const auto issue_text = read_text(issue_path);
        expect(issue_text.find("type: Issue") != std::string::npos, "issue file did not round-trip Issue type");
        expect(issue_text.find("Quick smoke issue non-goal.") != std::string::npos, "issue file did not record non-goals alias text");
        expect(issue_text.find("2026-06-20: Quick smoke issue amendment.") != std::string::npos, "issue file did not record intent amendments text");
        expect(issue_text.find("state: InProgress") != std::string::npos, "issue file did not record InProgress state");
        expect(issue_text.find("Issue worklog smoke") != std::string::npos, "issue file did not record worklog");

        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "bug", "--title", "Raw start Ready gate fixture", "--agent", "tester"}, "Raw start Ready gate fixture")) == 0,
            "workitem create raw start bug failed");
        const auto raw_start_bug_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "bug" / "0000" / "QS-BUG-0001_raw-start-ready-gate-fixture.md";
        const auto raw_start_rejected_output = temp_root / "raw-start-rejected.txt";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "state", "transition", "QS-BUG-0001", "start", "--agent", "tester"
        }, raw_start_rejected_output) != 0, "raw start should reject an incomplete item");
        expect(read_text(raw_start_rejected_output).find("is not Ready") != std::string::npos,
            "raw start rejection should explain the Ready gate failure");
        expect(read_text(raw_start_bug_path).find("state: Proposed") != std::string::npos,
            "rejected raw start should leave the item Proposed");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-BUG-0001",
            "--context", "Raw start gate context.",
            "--goal", "Raw start gate goal.",
            "--non-goals", "Raw start gate non-goal.",
            "--approach", "Raw start gate approach.",
            "--acceptance-criteria", "Raw start gate acceptance.",
            "--risks", "Raw start gate risks.",
            "--agent", "tester"
        }) == 0, "raw start bug set-ready failed");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "state", "transition", "QS-BUG-0001", "start",
            "--agent", "tester", "--message", "Start after Ready validation."
        }) == 0, "raw start should accept a Ready item");
        expect(read_text(raw_start_bug_path).find("state: InProgress") != std::string::npos,
            "accepted raw start should persist InProgress state");

        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "feature", "--title", "Quick smoke parent feature", "--agent", "tester"}, "Quick smoke parent feature")) == 0,
            "workitem create parent feature failed");
        const auto parent_feature_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "feature" / "0000" / "QS-FTR-0001_quick-smoke-parent-feature.md";
        const auto update_state_help_output = temp_root / "update-state-help.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "update-state", "--help"
            }, update_state_help_output),
            update_state_help_output,
            "update-state help failed");
        expect(
            read_text(update_state_help_output).find("--no-sync-parent") != std::string::npos,
            "update-state help should expose the documented parent-sync opt-out");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "set-ready", "QS-FTR-0001",
            "--context", "Parent feature context.",
            "--goal", "Parent feature goal.",
            "--non-goals", "Parent feature non-goal.",
            "--acceptance-criteria", "Parent feature acceptance.",
            "--intent-amendments", "2026-06-20: Parent feature amendment.",
            "--agent", "tester"}) == 0,
            "parent feature set-ready failed");
        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Quick smoke child task", "--parent", "QS-FTR-0001", "--agent", "tester"}, "Quick smoke child task")) == 0,
            "workitem create child task failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "set-ready", "QS-TSK-0002",
            "--context", "Child task context.",
            "--goal", "Child task goal.",
            "--approach", "Child task approach. Reactivation dogfood evidence: KOB-TSK-0025, KOB-TSK-0006, KOB-BUG-0020, KOB-TSK-0028, KOB-TSK-0030, KOB-TSK-0001, KOB-TSK-0004.",
            "--acceptance-criteria", "Child task acceptance.",
            "--risks", "Child task risks.",
            "--agent", "tester"}) == 0,
            "child task set-ready failed");

        const auto parent_missing_admission_output = temp_root / "work-order-admission-parent-missing.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "work-order-admission", "QS-FTR-0001", "--format", "json"
            }, parent_missing_admission_output),
            parent_missing_admission_output,
            "parent missing work-order-admission json failed"
        );
        const auto parent_missing_admission_json = read_text(parent_missing_admission_output);
        expect(parent_missing_admission_json.find("\"admitted\" : false") != std::string::npos, "parent missing admission should be blocked");
        expect(parent_missing_admission_json.find("\"requires_explicit_intent\" : true") != std::string::npos, "parent missing admission should require explicit intent");
        expect(parent_missing_admission_json.find("parent_explicit_intent_required") != std::string::npos, "parent missing admission should explain explicit intent requirement");
        expect(parent_missing_admission_json.find("\"starts_agent\" : false") != std::string::npos, "parent missing admission should not start agents");
        expect(parent_missing_admission_json.find("\"dispatches_work\" : false") != std::string::npos, "parent missing admission should not dispatch work");

        const auto parent_implementation_admission_output = temp_root / "work-order-admission-parent-implementation.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "work-order-admission", "QS-FTR-0001", "--intent", "implementation", "--format", "json"
            }, parent_implementation_admission_output),
            parent_implementation_admission_output,
            "parent implementation work-order-admission json failed"
        );
        const auto parent_implementation_admission_json = read_text(parent_implementation_admission_output);
        expect(parent_implementation_admission_json.find("parent_implementation_blocked_ready_gate_child") != std::string::npos,
            "parent implementation admission should route proposed child through Ready gate");
        expect(parent_implementation_admission_json.find("QS-TSK-0002") != std::string::npos,
            "parent implementation admission should list child task candidate");
        expect(parent_implementation_admission_json.find("ready_gate_child") != std::string::npos,
            "parent implementation admission should include child recommendation");

        const auto parent_planning_admission_output = temp_root / "work-order-admission-parent-planning.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "work-order-admission", "QS-FTR-0001", "--intent", "planning"
            }, parent_planning_admission_output),
            parent_planning_admission_output,
            "parent planning work-order-admission text failed"
        );
        const auto parent_planning_admission_text = read_text(parent_planning_admission_output);
        expect(parent_planning_admission_text.find("admitted: true") != std::string::npos, "parent planning admission text should allow planning");
        expect(parent_planning_admission_text.find("starts_agent: false") != std::string::npos, "parent planning admission text should remain read-only");

        const auto task_admission_output = temp_root / "work-order-admission-task.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "work-order-admission", "QS-TSK-0002", "--intent", "implementation", "--format", "json"
            }, task_admission_output),
            task_admission_output,
            "task work-order-admission json failed"
        );
        const auto task_admission_json = read_text(task_admission_output);
        expect(task_admission_json.find("\"admitted\" : true") != std::string::npos, "task implementation admission should be allowed");
        expect(task_admission_json.find("\"would_dispatch\" : true") != std::string::npos, "task implementation admission should report would_dispatch");
        expect(task_admission_json.find("\"starts_agent\" : false") != std::string::npos, "task implementation admission should not start an agent during diagnostic");
        expect(task_admission_json.find("\"dispatches_work\" : false") != std::string::npos, "task implementation admission should not dispatch work during diagnostic");

        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "sub-task", "--title", "Quick smoke subtask", "--parent", "QS-TSK-0002", "--agent", "tester"}, "Quick smoke subtask")) == 0,
            "workitem create subtask failed");
        const auto subtask_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "subtask" / "0000" / "QS-SUBTSK-0001_quick-smoke-subtask.md";
        expect(std::filesystem::exists(subtask_path), "workitem create did not create expected subtask file");
        expect(read_text(subtask_path).find("type: SubTask") != std::string::npos, "subtask file did not materialize SubTask type");
        expect(read_text(subtask_path).find("parent: QS-TSK-0002") != std::string::npos, "subtask file did not preserve Task parent");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-SUBTSK-0001",
            "--context", "Quick smoke subtask context.",
            "--goal", "Quick smoke subtask goal.",
            "--approach", "Quick smoke subtask approach.",
            "--acceptance-criteria", "Quick smoke subtask acceptance.",
            "--risks", "Quick smoke subtask risks.",
            "--agent", "tester"
        }) == 0, "subtask set-ready failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "check-ready", "QS-SUBTSK-0001"}) == 0,
            "subtask check-ready failed");
        const auto subtask_list_output = temp_root / "subtask-list.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "list", "--type", "sub_task"
            }, subtask_list_output),
            subtask_list_output,
            "subtask list failed"
        );
        expect(read_text(subtask_list_output).find("QS-SUBTSK-0001") != std::string::npos,
            "subtask list did not include created subtask");
        const auto subtask_admission_output = temp_root / "work-order-admission-subtask.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "work-order-admission", "QS-SUBTSK-0001", "--intent", "implementation", "--format", "json"
            }, subtask_admission_output),
            subtask_admission_output,
            "subtask work-order-admission json failed"
        );
        const auto subtask_admission_json = read_text(subtask_admission_output);
        expect(subtask_admission_json.find("\"admitted\" : true") != std::string::npos, "subtask implementation admission should be allowed");
        expect(subtask_admission_json.find("\"item_type\" : \"SubTask\"") != std::string::npos, "subtask admission should report SubTask item type");
        const auto intent_stack_json_output = temp_root / "intent-stack.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-stack", "QS-TSK-0002", "--format", "json"
            }, intent_stack_json_output),
            intent_stack_json_output,
            "intent-stack json failed"
        );
        const auto intent_stack_json_text = read_text(intent_stack_json_output);
        const auto intent_stack_payload = read_json(intent_stack_json_output);
        expect(intent_stack_json_text.find("\"status\" : \"complete\"") != std::string::npos, "intent-stack json should be complete");
        expect(intent_stack_json_text.find("QS-TSK-0002") != std::string::npos, "intent-stack json should include current task");
        expect(intent_stack_json_text.find("QS-FTR-0001") != std::string::npos, "intent-stack json should include parent feature");
        expect(intent_stack_json_text.find("Parent feature non-goal.") != std::string::npos, "intent-stack json should include parent non-goals");
        expect(intent_stack_json_text.find("Parent feature amendment.") != std::string::npos, "intent-stack json should include parent amendments");
        for (const auto& dogfood_id : reactivation_dogfood_ids) {
            expect(json_array_contains_string(intent_stack_payload["evidence_refs"], dogfood_id),
                "intent-stack evidence contract should retain reactivation dogfood ref " + dogfood_id);
        }

        const auto intent_stack_text_output = temp_root / "intent-stack.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-stack", "QS-TSK-0002", "--format", "text", "--max-depth", "1"
            }, intent_stack_text_output),
            intent_stack_text_output,
            "intent-stack text failed"
        );
        const auto intent_stack_text = read_text(intent_stack_text_output);
        expect(intent_stack_text.find("# Intent Stack") != std::string::npos, "intent-stack text should include heading");
        expect(intent_stack_text.find("Parent-chain depth limit reached") != std::string::npos, "intent-stack text should warn at depth limit");

        const auto intent_template_text_output = temp_root / "intent-template.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-template", "QS-TSK-0002", "--kind", "both", "--format", "text"
            }, intent_template_text_output),
            intent_template_text_output,
            "intent-template text failed"
        );
        const auto intent_template_text = read_text(intent_template_text_output);
        expect(intent_template_text.find("# Intent Preflight") != std::string::npos, "intent-template text should include preflight heading");
        expect(intent_template_text.find("## Intent Trace") != std::string::npos, "intent-template text should include intent trace");
        expect(intent_template_text.find("## Inherited Do Not") != std::string::npos, "intent-template text should include inherited Do Not");
        expect(intent_template_text.find("Parent feature non-goal.") != std::string::npos, "intent-template text should include parent non-goals");
        expect(intent_template_text.find("Parent feature amendment.") != std::string::npos, "intent-template text should include intent amendments");
        expect(intent_template_text.find("# Do Not Compliance Report") != std::string::npos, "intent-template text should include compliance heading");
        expect(intent_template_text.find("OK/WARN/VIOLATION") != std::string::npos, "intent-template text should include compliance status prompts");
        expect(intent_template_text.find("## Reactivation Review") != std::string::npos,
            "intent-template text should include Reactivation Review");
        expect(intent_template_text.find("## Stale Solution Check") != std::string::npos,
            "intent-template text should include Stale Solution Check");
        expect(count_occurrences(intent_template_text, "## Reactivation Review") == 1,
            "intent-template text should emit Reactivation Review exactly once");
        expect(count_occurrences(intent_template_text, "## Stale Solution Check") == 1,
            "intent-template text should emit Stale Solution Check exactly once");
        expect(intent_template_text.find("Bug evidence can remain valid") != std::string::npos,
            "intent-template text should preserve bug evidence separately from stale solutions");
        expect(intent_template_text.find("old proposed fix is untrusted") != std::string::npos,
            "intent-template text should distrust old proposed fixes until revalidated");
        for (const auto* state_label : {"Proposed:", "Ready:", "InProgress:", "Review:", "Done/Post-Done:"}) {
            expect(intent_template_text.find(state_label) != std::string::npos,
                std::string("intent-template text should cover reactivation state ") + state_label);
        }

        const auto intent_template_json_output = temp_root / "intent-template.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-template", "QS-TSK-0002", "--kind", "preflight", "--format", "json"
            }, intent_template_json_output),
            intent_template_json_output,
            "intent-template json failed"
        );
        const auto intent_template_json = read_text(intent_template_json_output);
        expect(intent_template_json.find("\"kind\" : \"preflight\"") != std::string::npos, "intent-template json should identify preflight kind");
        expect(intent_template_json.find("\"preflight\"") != std::string::npos, "intent-template json should include preflight object");
        expect(intent_template_json.find("\"inherited_do_not\"") != std::string::npos, "intent-template json should include inherited_do_not");
        expect(intent_template_json.find("\"intent_amendments\"") != std::string::npos, "intent-template json should include intent amendments");
        expect(intent_template_json.find("\"reactivation_review\"") != std::string::npos,
            "intent-template json should include reactivation review protocol");
        expect(intent_template_json.find("\"state_matrix\"") != std::string::npos,
            "intent-template json should include reactivation state matrix");
        expect(intent_template_json.find("\"Done/Post-Done\"") != std::string::npos,
            "intent-template json should cover Done/Post-Done reactivation");
        expect(intent_template_json.find("\"stale_solution_check\"") != std::string::npos,
            "intent-template json should include stale solution protocol");
        expect(intent_template_json.find("\"bug_evidence\"") != std::string::npos &&
                   intent_template_json.find("\"proposed_fix\"") != std::string::npos,
            "intent-template json should distinguish bug evidence from the proposed fix");

        const auto intent_handoff_output = temp_root / "intent-handoff.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-template", "QS-TSK-0002", "--kind", "handoff", "--format", "text"
            }, intent_handoff_output),
            intent_handoff_output,
            "intent-template handoff failed"
        );
        const auto intent_handoff_text = read_text(intent_handoff_output);
        expect(intent_handoff_text.find("# Coding Agent Intent Prompt") != std::string::npos, "handoff template should include coding-agent heading");
        expect(intent_handoff_text.find("Do not infer final intent from raw backlog evidence") != std::string::npos, "handoff template should include execution boundary");

        const auto intent_handoff_json_output = temp_root / "intent-handoff.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-template", "QS-TSK-0002", "--kind", "handoff", "--format", "json"
            }, intent_handoff_json_output),
            intent_handoff_json_output,
            "intent-template handoff json failed"
        );
        const auto intent_handoff_json = read_text(intent_handoff_json_output);
        expect(intent_handoff_json.find("\"do_not_non_goals\"") != std::string::npos, "handoff json should include do_not_non_goals");
        expect(intent_handoff_json.find("Parent feature non-goal.") != std::string::npos, "handoff json should include inherited Do Not text");

        const auto drift_resolution_output = temp_root / "drift-resolution-template.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "drift-resolution-template", "QS-TSK-0002",
                "--drift-type", "stale proposed fix",
                "--detection-stage", "Review",
                "--detected-by", "ChatGPT"
            }, drift_resolution_output),
            drift_resolution_output,
            "drift-resolution-template failed"
        );
        const auto drift_resolution_text = read_text(drift_resolution_output);
        expect(drift_resolution_text.find("Detected drift is not execution permission") != std::string::npos, "drift resolution template should state execution boundary");
        expect(drift_resolution_text.find("- relates: QS-TSK-0002") != std::string::npos, "drift resolution template should link source item");

        const auto create_drift_dry_run_output = temp_root / "create-drift-resolution-dry-run.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "create-drift-resolution", "QS-TSK-0002",
                "--drift-type", "semantic evidence conflict",
                "--agent", "tester"
            }, create_drift_dry_run_output),
            create_drift_dry_run_output,
            "create-drift-resolution dry run failed"
        );
        expect(read_text(create_drift_dry_run_output).find("DRY RUN: would create") != std::string::npos, "create-drift-resolution should default to dry-run");

        const auto outside_item_path = temp_root / "outside-drift-source.md";
        write_text(outside_item_path, "outside product root should not be read or mutated\n");
        const auto outside_template_output = temp_root / "outside-intent-template.txt";
        const auto outside_template_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "intent-template", outside_item_path.string(), "--kind", "handoff"
        }, outside_template_output);
        expect(outside_template_rc != 0, "outside product-root path should be rejected for intent-template");
        expect(read_text(outside_template_output).find("outside active product root") != std::string::npos, "outside intent-template rejection should explain product-root boundary");

        const auto outside_admission_output = temp_root / "outside-work-order-admission.txt";
        const auto outside_admission_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "work-order-admission", outside_item_path.string(), "--intent", "implementation"
        }, outside_admission_output);
        expect(outside_admission_rc != 0, "outside product-root path should be rejected for work-order-admission");
        expect(read_text(outside_admission_output).find("outside active product root") != std::string::npos, "outside work-order-admission rejection should explain product-root boundary");

        const auto outside_apply_output = temp_root / "outside-create-drift-resolution-apply.txt";
        const auto outside_apply_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "create-drift-resolution", outside_item_path.string(), "--apply", "--agent", "tester"
        }, outside_apply_output);
        expect(outside_apply_rc != 0, "outside product-root path should be rejected before create-drift-resolution apply");
        expect(read_text(outside_apply_output).find("outside active product root") != std::string::npos, "outside apply rejection should explain product-root boundary");

        const auto outside_set_ready_output = temp_root / "outside-set-ready.txt";
        const auto outside_set_ready_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", outside_item_path.string(),
            "--context", "outside product root must not be mutated", "--agent", "tester"
        }, outside_set_ready_output);
        expect(outside_set_ready_rc != 0, "outside product-root path should be rejected for set-ready");
        expect(read_text(outside_set_ready_output).find("outside active product root") != std::string::npos, "outside set-ready rejection should explain product-root boundary");

        const auto outside_update_state_output = temp_root / "outside-update-state.txt";
        const auto outside_update_state_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", outside_item_path.string(),
            "--state", "Review", "--agent", "tester", "--force"
        }, outside_update_state_output);
        expect(outside_update_state_rc != 0, "outside product-root path should be rejected for update-state");
        expect(read_text(outside_update_state_output).find("outside active product root") != std::string::npos, "outside update-state rejection should explain product-root boundary");

        const auto apply_without_agent_output = temp_root / "create-drift-resolution-apply-no-agent.txt";
        const auto apply_without_agent_rc = run_command_capture(binary, {
            "-P", "quick-smoke-product", "workitem", "create-drift-resolution", "QS-TSK-0002", "--apply"
        }, apply_without_agent_output);
        expect(apply_without_agent_rc != 0, "create-drift-resolution --apply should require --agent before writing");
        expect(read_text(apply_without_agent_output).find("--agent is required with --apply") != std::string::npos, "apply without agent should report required agent");

        const auto no_drift_preflight_output = temp_root / "intent-drift-preflight-no-drift.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-drift-preflight", "QS-TSK-0002",
                "--result", "no-drift"
            }, no_drift_preflight_output),
            no_drift_preflight_output,
            "intent-drift-preflight no-drift failed"
        );
        const auto no_drift_preflight_text = read_text(no_drift_preflight_output);
        expect(no_drift_preflight_text.find("result: no drift detected") != std::string::npos, "no-drift preflight should allow normal handoff");
        expect(no_drift_preflight_text.find("handoff allowed: yes") != std::string::npos, "no-drift preflight should state handoff allowed");
        expect(no_drift_preflight_text.find("## Deterministic Evidence Checked") != std::string::npos, "preflight should materialize deterministic evidence checks");
        expect(no_drift_preflight_text.find("parent related tickets") != std::string::npos, "preflight should report parent related tickets");
        expect(no_drift_preflight_text.find("## Reactivation Review") != std::string::npos,
            "preflight should emit Reactivation Review");
        expect(no_drift_preflight_text.find("Bug evidence can remain valid") != std::string::npos,
            "preflight should preserve bug evidence independently of a stale proposed fix");
        expect(count_occurrences(no_drift_preflight_text, "## Reactivation Review") == 1,
            "preflight should emit Reactivation Review exactly once");
        expect(count_occurrences(no_drift_preflight_text, "## Stale Solution Check") == 1,
            "preflight should emit Stale Solution Check exactly once");

        const auto drift_preflight_output = temp_root / "intent-drift-preflight-drift.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "handoff-preflight", "QS-TSK-0002",
                "--result", "drift"
            }, drift_preflight_output),
            drift_preflight_output,
            "handoff-preflight drift failed"
        );
        const auto drift_preflight_text = read_text(drift_preflight_output);
        expect(drift_preflight_text.find("result: drift detected") != std::string::npos, "drift preflight should identify drift");
        expect(drift_preflight_text.find("Intent Drift Resolution ticket") != std::string::npos, "drift preflight should require resolution ticket");

        const auto uncertain_preflight_output = temp_root / "intent-drift-preflight-uncertain.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "codex-handoff-preflight", "QS-TSK-0002",
                "--result", "uncertain"
            }, uncertain_preflight_output),
            uncertain_preflight_output,
            "codex-handoff-preflight uncertain failed"
        );
        expect(read_text(uncertain_preflight_output).find("human confirmation required") != std::string::npos, "uncertain preflight should require human confirmation");

        const auto unknown_preflight_json_output = temp_root / "intent-drift-preflight-unknown.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-drift-preflight", "QS-TSK-0002",
                "--result", "unexpected-value", "--format", "json"
            }, unknown_preflight_json_output),
            unknown_preflight_json_output,
            "intent-drift-preflight unknown json failed"
        );
        const auto unknown_preflight_json = read_text(unknown_preflight_json_output);
        const auto unknown_preflight_payload = read_json(unknown_preflight_json_output);
        expect(unknown_preflight_json.find("\"result\" : \"uncertain\"") != std::string::npos, "unknown preflight result should normalize to uncertain");
        expect(unknown_preflight_json.find("\"explicit_blocks\"") != std::string::npos, "preflight json should include explicit blocks evidence");
        expect(unknown_preflight_json.find("\"recent_worklog\"") != std::string::npos, "preflight json should include worklog/history evidence");
        expect(unknown_preflight_json.find("\"reactivation_review\"") != std::string::npos &&
                   unknown_preflight_json.find("\"stale_solution_check\"") != std::string::npos,
            "preflight json should include reactivation and stale solution protocols");
        for (const auto& dogfood_id : reactivation_dogfood_ids) {
            expect(json_array_contains_string(
                       unknown_preflight_payload["intent_stack"]["evidence_refs"], dogfood_id),
                "preflight evidence contract should retain reactivation dogfood ref " + dogfood_id);
        }

        const auto proposed_amend_output = temp_root / "intent-amend-proposed.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0002",
                "--correction", "Proposed correction one.",
                "--reason", "Human clarified task scope.",
                "--applies-to", "Approach",
                "--agent", "tester",
                "--format", "json"
            }, proposed_amend_output),
            proposed_amend_output,
            "intent-amend proposed failed"
        );
        const auto proposed_amend_json = read_text(proposed_amend_output);
        expect(proposed_amend_json.find("\"appended\" : true") != std::string::npos, "intent-amend json should report append");
        expect(proposed_amend_json.find("Clarify Ready fields directly") != std::string::npos, "proposed amendment should emit ready-field guidance");
        auto amended_task_text = read_text(temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "0000" / "QS-TSK-0002_quick-smoke-child-task.md");
        expect(amended_task_text.find("Proposed correction one.") != std::string::npos, "intent-amend should append correction text");
        expect(amended_task_text.find("applies_to: Approach") != std::string::npos, "intent-amend should record applies_to metadata");
        expect(amended_task_text.find("Intent Amendment appended: Human clarified task scope.") != std::string::npos, "intent-amend should append worklog evidence");

        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0002",
            "--state", "InProgress", "--agent", "tester", "--force", "--no-sync-parent"
        }) == 0,
            "intent-amend child update InProgress failed");
        const auto parent_after_no_sync = read_text(parent_feature_path);
        expect(
            parent_after_no_sync.find("state: Proposed") != std::string::npos,
            "CLI --no-sync-parent should leave the eligible parent Proposed");
        expect(
            parent_after_no_sync.find("Auto parent sync: child QS-TSK-0002") == std::string::npos,
            "CLI --no-sync-parent should not append an auto-sync parent worklog");
        const auto inprogress_amend_output = temp_root / "intent-amend-inprogress.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0002",
                "--correction", "InProgress correction requires replanning.",
                "--reason", "Implementation drift detected.",
                "--applies-to", "Plan",
                "--agent", "tester"
            }, inprogress_amend_output),
            inprogress_amend_output,
            "intent-amend inprogress failed"
        );
        expect(read_text(inprogress_amend_output).find("Needs replan") != std::string::npos, "inprogress amendment should emit replan guidance");

        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0002", "--state", "Review", "--agent", "tester", "--force"}) == 0,
            "intent-amend child update Review failed");
        expect(
            read_text(parent_feature_path).find("state: InProgress") != std::string::npos,
            "default CLI update-state should retain forward-only parent synchronization");
        const auto review_amend_output = temp_root / "intent-amend-review.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0002",
                "--correction", "Review correction blocks Done.",
                "--reason", "Reviewer found drift.",
                "--applies-to", "Acceptance Criteria",
                "--agent", "tester"
            }, review_amend_output),
            review_amend_output,
            "intent-amend review failed"
        );
        expect(read_text(review_amend_output).find("Drift finding") != std::string::npos, "review amendment should emit drift-finding guidance");

        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0002", "--state", "Done", "--agent", "tester", "--force"}) == 0,
            "intent-amend child update Done failed");
        const auto done_amend_output = temp_root / "intent-amend-done.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0002",
                "--correction", "Done correction needs follow-up.",
                "--reason", "Post-done violation found.",
                "--applies-to", "Validation",
                "--agent", "tester"
            }, done_amend_output),
            done_amend_output,
            "intent-amend done failed"
        );
        expect(read_text(done_amend_output).find("Post-done drift") != std::string::npos, "done amendment should emit post-done guidance");
        amended_task_text = read_text(temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "0000" / "QS-TSK-0002_quick-smoke-child-task.md");
        expect(amended_task_text.find("Proposed correction one.") < amended_task_text.find("InProgress correction requires replanning."), "intent amendments should remain append-only in order");
        expect(amended_task_text.find("InProgress correction requires replanning.") < amended_task_text.find("Review correction blocks Done."), "review amendment should append after in-progress amendment");
        expect(amended_task_text.find("Review correction blocks Done.") < amended_task_text.find("Done correction needs follow-up."), "done amendment should append last");

        expect(run_command(binary, with_duplicate_admission({
            "-P", "quick-smoke-product", "item", "create", "-t", "task",
            "--title", "Duplicate intent amendment fixture", "--agent", "tester"
        }, "Duplicate intent amendment fixture")) == 0, "duplicate intent amendment fixture creation failed");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0003",
            "--state", "Duplicate", "--duplicate-of", "QS-TSK-0001", "--agent", "tester"
        }) == 0, "duplicate intent amendment fixture transition failed");
        const auto duplicate_amend_output = temp_root / "intent-amend-duplicate.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-amend", "QS-TSK-0003",
                "--correction", "Duplicate evidence correction.",
                "--reason", "Historical audit clarification.",
                "--applies-to", "Evidence",
                "--agent", "tester"
            }, duplicate_amend_output),
            duplicate_amend_output,
            "intent-amend duplicate failed"
        );
        const auto duplicate_amend_text = read_text(duplicate_amend_output);
        expect(duplicate_amend_text.find("Duplicate item amended for audit only") != std::string::npos,
            "duplicate amendment should emit audit-only guidance");
        expect(duplicate_amend_text.find("canonical item") != std::string::npos,
            "duplicate amendment should direct execution to the canonical item");

        const auto topic_output = temp_root / "topic-list-templates.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "topic", "create", "quick-topic-template-list-placeholder",
                "--agent", "tester", "--list-templates", "--format", "json"
            }, topic_output),
            topic_output,
            "topic create --list-templates failed"
        );
        expect(read_text(topic_output).find("\"builtin_count\"") != std::string::npos,
            "topic create --list-templates did not emit builtin_count");

        // Test config migrate-prefix (dry-run planner)
        const auto migpf_ok_output = temp_root / "migpf_ok.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "config", "migrate-prefix", "--to", "NEWQS"
            }, migpf_ok_output),
            migpf_ok_output,
            "config migrate-prefix failed"
        );
        const auto migpf_ok_text = read_text(migpf_ok_output);
        expect(migpf_ok_text.find("\"valid\" : true") != std::string::npos, "migrate-prefix should be valid");
        expect(migpf_ok_text.find("\"from_prefix\" : \"QS\"") != std::string::npos, "migrate-prefix from_prefix should be QS");
        expect(migpf_ok_text.find("\"to_prefix\" : \"NEWQS\"") != std::string::npos, "migrate-prefix to_prefix should be NEWQS");
        expect(migpf_ok_text.find("QS-TSK-0001 -> NEWQS-TSK-0001") != std::string::npos, "migrate-prefix should update references");
        expect(migpf_ok_text.find("QS-TSK-0002 -> NEWQS-TSK-0002") != std::string::npos, "migrate-prefix should update child references");
        const auto migpf_fail_output = temp_root / "migpf_fail.json";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "config", "migrate-prefix"
        }, migpf_fail_output) != 0, "migrate-prefix without --to should fail");
        const auto migpf_fail_text = read_text(migpf_fail_output);
        expect(migpf_fail_text.find("\"valid\" : false") != std::string::npos, "migrate-prefix without --to should be invalid");
        expect(migpf_fail_text.find("Target prefix (--to) is required") != std::string::npos, "migrate-prefix should list missing target error");

        const auto migpf_bad_to_output = temp_root / "migpf_bad_to.json";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "config", "migrate-prefix", "--to", "123QS"
        }, migpf_bad_to_output) != 0, "migrate-prefix with invalid grammar --to should fail");
        const auto migpf_bad_to_text = read_text(migpf_bad_to_output);
        expect(migpf_bad_to_text.find("\"valid\" : false") != std::string::npos, "migrate-prefix with invalid grammar --to should be invalid");
        expect(migpf_bad_to_text.find("New prefix does not match grammar") != std::string::npos, "migrate-prefix should list grammar error");

        const auto migpf_bad_from_output = temp_root / "migpf_bad_from.json";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "config", "migrate-prefix", "--to", "NEWQS", "--from", "WRONG"
        }, migpf_bad_from_output) != 0, "migrate-prefix with mismatching --from should fail");
        const auto migpf_bad_from_text = read_text(migpf_bad_from_output);
        expect(migpf_bad_from_text.find("\"valid\" : false") != std::string::npos, "migrate-prefix with mismatching --from should be invalid");
        expect(migpf_bad_from_text.find("does not match resolved prefix") != std::string::npos, "migrate-prefix should list mismatching from error");

        // Step 67: apply migration with --write
        const auto migpf_apply_output = temp_root / "migpf_apply.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "config", "migrate-prefix", "--to", "NEWQS2", "--write"
            }, migpf_apply_output),
            migpf_apply_output,
            "config migrate-prefix --write failed"
        );
        const auto migpf_apply_text = read_text(migpf_apply_output);
        expect(migpf_apply_text.find("\"status\" : \"applied\"") != std::string::npos, "apply should report applied status");
        expect(migpf_apply_text.find("\"valid\" : true") != std::string::npos, "apply should be valid");
        expect(migpf_apply_text.find("\"from_prefix\" : \"QS\"") != std::string::npos, "apply from_prefix should be QS");
        expect(migpf_apply_text.find("\"to_prefix\" : \"NEWQS2\"") != std::string::npos, "apply to_prefix should be NEWQS2");
        expect(migpf_apply_text.find("\"items_renamed\"") != std::string::npos, "apply should report items_renamed count");
        expect(
            !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_Active.md") &&
                !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_New.md") &&
                !std::filesystem::exists(quick_views_root / "Dashboard_" "PlainMarkdown_Done.md"),
            "migrate-prefix should not generate plain Markdown dashboards");

        // Step 68: verify --write blocked when --from mismatches (prefix was already changed to NEWQS2)
        const auto migpf_write_fail_output = temp_root / "migpf_write_fail.json";
        expect(run_command_capture(binary, {
            "-P", "quick-smoke-product", "config", "migrate-prefix", "--to", "NEWQS3", "--from", "QS", "--write"
        }, migpf_write_fail_output) != 0, "migrate-prefix --write with mismatched --from should fail and not apply");
        const auto migpf_write_fail_text = read_text(migpf_write_fail_output);
        expect(migpf_write_fail_text.find("apply-blocked") != std::string::npos || migpf_write_fail_text.find("\"valid\" : false") != std::string::npos,
            "apply-blocked output should report invalid plan");

        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(temp_root);
        std::cout << "cli_quick_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_quick_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
