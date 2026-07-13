#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

        expect(run_command(binary, {"--help"}) == 0, "help command failed");
        expect(run_command(binary, {"--version"}) == 0, "version command failed");
        expect(run_command(binary, {"gui", "--help"}) == 0, "gui help failed");
        expect(run_command(binary, {"webview", "--help"}) == 0, "webview help failed");
        expect(run_command(binary, {"admin", "items", "--help"}) == 0, "admin items help failed");
        expect(run_command(binary, {"admin", "validate", "--help"}) == 0, "admin validate help failed");

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto temp_root = std::filesystem::temp_directory_path() / ("kano-backlog-cli-quick-smoke-" + std::to_string(unique));
        std::filesystem::create_directories(temp_root);
        const auto original_cwd = std::filesystem::current_path();
        std::filesystem::current_path(temp_root);

        expect(run_command(binary, {"admin", "init", "--product", "quick-smoke-product", "--agent", "tester", "--skip-refresh-views"}) == 0,
            "admin init command failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "admin", "sync-sequences"}) == 0,
            "sync-sequences failed");
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Missing duplicate admission", "--agent", "tester"}) != 0,
            "workitem create without duplicate admission should fail");
        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Quick smoke task", "--agent", "tester", "--profile-mutations"}, "Quick smoke task")) == 0,
            "workitem create failed");
        const auto task_receipt_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "_meta" / "duplicate-admission" / "QS-TSK-0001.json";
        expect(std::filesystem::exists(task_receipt_path), "workitem create should write duplicate admission receipt");

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
        const auto task_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "task" / "0000" / "QS-TSK-0001_quick-smoke-task.md";
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

        expect(run_command(binary, with_duplicate_admission({"-P", "quick-smoke-product", "workitem", "create", "-t", "feature", "--title", "Quick smoke parent feature", "--agent", "tester"}, "Quick smoke parent feature")) == 0,
            "workitem create parent feature failed");
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
            "--approach", "Child task approach.",
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
        expect(run_command(binary, {"-P", "quick-smoke-product", "view", "refresh", "--agent", "tester"}) == 0,
            "view refresh after subtask failed");
        expect(read_text(temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "views" / "Dashboard_PlainMarkdown_New.md").find("QS-SUBTSK-0001") != std::string::npos,
            "dashboard should include created subtask");

        const auto intent_stack_json_output = temp_root / "intent-stack.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-P", "quick-smoke-product", "workitem", "intent-stack", "QS-TSK-0002", "--format", "json"
            }, intent_stack_json_output),
            intent_stack_json_output,
            "intent-stack json failed"
        );
        const auto intent_stack_json_text = read_text(intent_stack_json_output);
        expect(intent_stack_json_text.find("\"status\" : \"complete\"") != std::string::npos, "intent-stack json should be complete");
        expect(intent_stack_json_text.find("QS-TSK-0002") != std::string::npos, "intent-stack json should include current task");
        expect(intent_stack_json_text.find("QS-FTR-0001") != std::string::npos, "intent-stack json should include parent feature");
        expect(intent_stack_json_text.find("Parent feature non-goal.") != std::string::npos, "intent-stack json should include parent non-goals");
        expect(intent_stack_json_text.find("Parent feature amendment.") != std::string::npos, "intent-stack json should include parent amendments");

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
        expect(unknown_preflight_json.find("\"result\" : \"uncertain\"") != std::string::npos, "unknown preflight result should normalize to uncertain");
        expect(unknown_preflight_json.find("\"explicit_blocks\"") != std::string::npos, "preflight json should include explicit blocks evidence");
        expect(unknown_preflight_json.find("\"recent_worklog\"") != std::string::npos, "preflight json should include worklog/history evidence");

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

        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "update-state", "QS-TSK-0002", "--state", "InProgress", "--agent", "tester", "--force"}) == 0,
            "intent-amend child update InProgress failed");
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
