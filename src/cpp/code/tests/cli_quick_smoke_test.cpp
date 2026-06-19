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
        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "create", "-t", "task", "--title", "Quick smoke task", "--agent", "tester"}) == 0,
            "workitem create failed");

        const auto text_root = temp_root / "ready-fields";
        write_text(text_root / "context.md", "Quick smoke context.\n");
        write_text(text_root / "goal.md", "Quick smoke goal.\n");
        write_text(text_root / "approach.md", "Quick smoke approach.\n");
        write_text(text_root / "acceptance.md", "Quick smoke acceptance.\n");
        write_text(text_root / "risks.md", "Quick smoke risks.\n");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-TSK-0001",
            "--context-file", (text_root / "context.md").string(),
            "--goal-file", (text_root / "goal.md").string(),
            "--approach-file", (text_root / "approach.md").string(),
            "--acceptance-criteria-file", (text_root / "acceptance.md").string(),
            "--risks-file", (text_root / "risks.md").string(),
            "--agent", "tester"
        }) == 0, "set-ready failed");

        expect(run_command(binary, {"-P", "quick-smoke-product", "workitem", "create", "-t", "issue", "--title", "Quick smoke issue", "--agent", "tester"}) == 0,
            "workitem create issue failed");
        const auto issue_path = temp_root / "_kano" / "backlog" / "products" / "quick-smoke-product" / "items" / "issue" / "0000" / "QS-ISS-0001_quick-smoke-issue.md";
        expect(std::filesystem::exists(issue_path), "workitem create did not create expected issue file");
        expect(run_command(binary, {
            "-P", "quick-smoke-product", "workitem", "set-ready", "QS-ISS-0001",
            "--context", "Quick smoke issue context.",
            "--goal", "Quick smoke issue goal.",
            "--approach", "Quick smoke issue approach.",
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
        expect(issue_text.find("state: InProgress") != std::string::npos, "issue file did not record InProgress state");
        expect(issue_text.find("Issue worklog smoke") != std::string::npos, "issue file did not record worklog");

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

        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(temp_root);
        std::cout << "cli_quick_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_quick_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
