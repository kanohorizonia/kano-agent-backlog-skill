#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

#include "kano/backlog_core/process/noninteractive_errors.hpp"

namespace {

int g_command_step = 0;

std::string shell_command_for(const std::filesystem::path& binary, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::string command = "cd /d \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
#else
    std::string command = "cd \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
#endif
    for (const auto& arg : args) {
        command += " ";
        command += "\"";
        command += arg;
        command += "\"";
    }
    return command;
}

void log_command_start(const std::vector<std::string>& args) {
    std::cout << "[cli_repo_smoke_test step " << ++g_command_step << "]";
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << std::endl;
}

void log_command_result(int rc, const std::filesystem::path& output_path = {}) {
    std::cout << "[cli_repo_smoke_test step " << g_command_step << "] exit=" << rc;
    if (!output_path.empty()) {
        std::cout << " output=" << output_path.string();
    }
    std::cout << std::endl;
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

void expect_command_capture_failure(
    int rc,
    const std::filesystem::path& output_path,
    const std::string& message,
    const std::string& expected_fragment
) {
    if (rc == 0) {
        throw std::runtime_error(message + " (command unexpectedly succeeded)");
    }

    const auto output = std::filesystem::exists(output_path) ? read_text(output_path) : std::string();
    if (!expected_fragment.empty() && output.find(expected_fragment) == std::string::npos) {
        std::ostringstream detail;
        detail << message << " (missing expected output fragment: " << expected_fragment << ")";
        if (!output.empty()) {
            detail << "\n--- command output ---\n" << output;
        }
        throw std::runtime_error(detail.str());
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << text;
}

std::string extract_json_array_string(const std::string& text, const std::string& key, std::size_t index) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        throw std::runtime_error("missing JSON key " + key);
    }
    const auto array_start = text.find('[', key_pos);
    const auto array_end = text.find(']', array_start);
    if (array_start == std::string::npos || array_end == std::string::npos) {
        throw std::runtime_error("missing JSON array for " + key);
    }
    std::size_t current = 0;
    std::size_t pos = array_start + 1;
    while (pos < array_end) {
        const auto quote_start = text.find('"', pos);
        if (quote_start == std::string::npos || quote_start >= array_end) {
            break;
        }
        const auto quote_end = text.find('"', quote_start + 1);
        if (quote_end == std::string::npos || quote_end > array_end) {
            break;
        }
        if (current == index) {
            return text.substr(quote_start + 1, quote_end - quote_start - 1);
        }
        ++current;
        pos = quote_end + 1;
    }
    throw std::runtime_error("missing JSON array value for " + key);
}

void set_env_var(const std::string& name, const std::string& value) {
#ifdef _WIN32
    if (_putenv_s(name.c_str(), value.c_str()) != 0) {
        throw std::runtime_error("failed to set env var " + name);
    }
#else
    if (setenv(name.c_str(), value.c_str(), 1) != 0) {
        throw std::runtime_error("failed to set env var " + name);
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    try {
        const std::filesystem::path repo_root(KANO_REPO_ROOT);
        const std::filesystem::path executable_path =
            (argc > 0 && argv != nullptr) ? std::filesystem::absolute(argv[0]) : std::filesystem::path();
        const std::string exe_suffix =
#ifdef _WIN32
            ".exe";
#else
            "";
#endif
        std::vector<std::filesystem::path> binary_candidates = {
            executable_path.parent_path() / ("kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/windows-ninja-msvc/release/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/linux-ninja-clang/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/linux-ninja-clang/release/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/linux-ninja-gcc/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/linux-ninja-gcc/release/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang/release/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang-x64/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang-x64/release/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang-arm64/debug/kano-backlog" + exe_suffix),
            repo_root / ("src/cpp/out/bin/macos-ninja-clang-arm64/release/kano-backlog" + exe_suffix)
        };
        std::filesystem::path binary;
        for (const auto& candidate : binary_candidates) {
            if (std::filesystem::exists(candidate)) {
                binary = candidate;
                break;
            }
        }

        expect(std::filesystem::exists(binary), "native binary not found for cli_repo_smoke_test");
        if (std::filesystem::exists(repo_root)) {
            std::filesystem::current_path(repo_root);
        } else {
            std::filesystem::current_path(binary.parent_path());
        }

        expect(run_command(binary, {"--help"}) == 0, "help command failed");
        expect(run_command(binary, {"--version"}) == 0, "version command failed");
        const auto hygiene_output = std::filesystem::temp_directory_path() / "kano-backlog-repo-hygiene-smoke.txt";
        expect(run_command_capture(binary, {"repo-hygiene", "check", "--archive-safe"}, hygiene_output) == 0, "repo-hygiene command failed");
        const auto hygiene_text = read_text(hygiene_output);
        expect(hygiene_text.find("kano-backlog repo hygiene summary") != std::string::npos, "repo-hygiene did not run native summary");
        expect(hygiene_text.find("no-op") == std::string::npos, "repo-hygiene still reports no-op");
        const auto export_dir = std::filesystem::temp_directory_path() / "kano-backlog-export-smoke";
        std::filesystem::remove_all(export_dir);
        std::filesystem::create_directories(export_dir);
        const auto export_output = std::filesystem::temp_directory_path() / "kano-backlog-export-smoke.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "export",
                "--single",
                "--no-validate-release-archive",
                "--repo",
                repo_root.string(),
                "--output",
                export_dir.string()
            }, export_output),
            export_output,
            "export command failed"
        );
        const auto export_text = read_text(export_output);
        expect(export_text.find("Archive created:") != std::string::npos, "export did not create an archive");
        expect(export_text.find("no-op") == std::string::npos, "export still reports no-op");

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path() / ("kano-backlog-cli-init-smoke-" + std::to_string(unique));
        std::filesystem::create_directories(temp_root);

        const auto original_cwd = std::filesystem::current_path();
        std::filesystem::current_path(temp_root);

        expect(run_command(binary, {"admin", "init", "--product", "kano-ai-3d-asset-skill", "--agent", "tester", "--skip-refresh-views"}) == 0, "admin init command failed");
        const auto duplicate_admin_init_output = temp_root / "admin-init-duplicate.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--product", "kano-ai-3d-asset-skill", "--agent", "tester", "--skip-refresh-views"}, duplicate_admin_init_output),
            duplicate_admin_init_output,
            "duplicate admin init should fail without --force",
            "Product backlog already exists"
        );
        expect(run_command(binary, {"admin", "init", "--product", "kano-ai-3d-asset-skill", "--agent", "tester", "--product-name", "Kano AI 3D Asset Skill", "--force", "--skip-refresh-views"}) == 0, "forced admin init with spaced product name failed");

        const auto config_path = temp_root / ".kano" / "backlog_config.toml";
        const auto backlog_root = temp_root / "_kano" / "backlog";
        const auto product_root = backlog_root / "products" / "kano-ai-3d-asset-skill";
        expect(std::filesystem::exists(config_path), "admin init did not create .kano/backlog_config.toml");
        expect(std::filesystem::exists(product_root / "decisions"), "admin init did not create decisions directory");
        expect(std::filesystem::exists(product_root / "views"), "admin init did not create views directory");
        expect(std::filesystem::exists(product_root / "_meta"), "admin init did not create _meta directory");
        expect(std::filesystem::exists(product_root / "artifacts"), "admin init did not create artifacts directory");
        for (const std::string type : {"epic", "feature", "userstory", "task", "bug"}) {
            expect(std::filesystem::exists(product_root / "items" / type / "0000"), "admin init did not create item bucket for " + type);
        }
        expect(!std::filesystem::exists(temp_root / "items"), "admin init created stray root-level items directory");

        const std::string config_text = read_text(config_path);
        expect(config_text.find("[products.kano-ai-3d-asset-skill]") != std::string::npos, "admin init did not register product in config");
        expect(config_text.find("backlog_root = \"_kano/backlog/products/kano-ai-3d-asset-skill\"") != std::string::npos, "admin init registered unexpected backlog_root");
        expect(config_text.find("name = \"Kano AI 3D Asset Skill\"") != std::string::npos, "admin init did not preserve spaced product display name");
        expect(config_text.find("[products.kano-ai-3d-asset-skill]", config_text.find("[products.kano-ai-3d-asset-skill]") + 1) == std::string::npos, "force admin init duplicated product config block");

        const std::string gitignore_text = read_text(temp_root / ".gitignore");
        expect(gitignore_text.find(".kano/cache/") != std::string::npos, "admin init did not add .kano/cache to .gitignore");
        expect(gitignore_text.find("_kano/backlog/_shared/logs/") != std::string::npos, "admin init did not add shared logs to .gitignore");

        const auto config_show_output = temp_root / "config-show.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "config", "show",
                "--path", temp_root.string(),
                "--product", "kano-ai-3d-asset-skill"
            }, config_show_output),
            config_show_output,
            "config show failed after spaced product-name init"
        );
        expect(read_text(config_show_output).find("Kano AI 3D Asset Skill") != std::string::npos, "config show did not reload spaced product name");

        const auto profile_path = temp_root / ".kano" / "backlog_config" / "embedding" / "local-noop.toml";
        write_text(profile_path, "[embedding]\nprovider = \"noop\"\nmodel = \"noop-embedding\"\n");
        const auto profiles_list_output = temp_root / "config-profiles-list.txt";
        expect(run_command_capture(binary, {
            "config", "profiles", "list",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill"
        }, profiles_list_output) == 0, "config profiles list failed");
        expect(read_text(profiles_list_output).find("embedding/local-noop") != std::string::npos, "config profiles list did not show profile");

        const auto profiles_show_output = temp_root / "config-profiles-show.json";
        expect(run_command_capture(binary, {
            "config", "profiles", "show", "embedding/local-noop",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill"
        }, profiles_show_output) == 0, "config profiles show failed");
        expect(read_text(profiles_show_output).find("\"raw_toml\"") != std::string::npos, "config profiles show did not emit raw TOML JSON");

        const auto config_pipeline_output = temp_root / "config-pipeline.txt";
        expect(run_command_capture(binary, {
            "config", "pipeline",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--profile", "embedding/local-noop"
        }, config_pipeline_output) == 0, "config pipeline failed");
        expect(read_text(config_pipeline_output).find("Pipeline config is valid") != std::string::npos, "config pipeline did not validate native pipeline");

        const auto config_validate_output = temp_root / "config-validate.txt";
        expect(run_command_capture(binary, {
            "config", "validate",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill"
        }, config_validate_output) == 0, "config validate failed");
        expect(read_text(config_validate_output).find("Config is valid") != std::string::npos, "config validate did not report success");

        const auto effective_config_json = temp_root / "effective-config.json";
        expect(run_command(binary, {
            "config", "export",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--format", "json",
            "--out", effective_config_json.string()
        }) == 0, "config export json failed");
        expect(read_text(effective_config_json).find("\"context\"") != std::string::npos, "config export json did not write context");

        const auto product_config_json = product_root / "_config" / "config.json";
        write_text(product_config_json, "{\n  \"product\": {\"name\": \"kano-ai-3d-asset-skill\", \"prefix\": \"KA\"},\n  \"embedding\": {\"provider\": \"noop\"}\n}\n");
        const auto migrate_dry_run_output = temp_root / "config-migrate-dry-run.json";
        expect(run_command_capture(binary, {
            "config", "migrate-json",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill"
        }, migrate_dry_run_output) == 0, "config migrate-json dry-run failed");
        expect(read_text(migrate_dry_run_output).find("\"status\" : \"dry-run\"") != std::string::npos, "config migrate-json dry-run did not plan migration");
        expect(!std::filesystem::exists(product_root / "_config" / "config.toml"), "config migrate-json dry-run wrote TOML");

        const auto migrate_write_output = temp_root / "config-migrate-write.json";
        expect(run_command_capture(binary, {
            "config", "migrate-json",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--write"
        }, migrate_write_output) == 0, "config migrate-json write failed");
        expect(read_text(migrate_write_output).find("\"status\" : \"written\"") != std::string::npos, "config migrate-json write did not report written");
        expect(std::filesystem::exists(product_root / "_config" / "config.toml"), "config migrate-json write did not write TOML");
        expect(std::filesystem::exists(product_root / "_config" / "config.json.bak"), "config migrate-json write did not write backup");

        expect(run_command(binary, {
            "config", "init",
            "--path", temp_root.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--prefix", "KAI",
            "--force"
        }) == 0, "config init failed");
        expect(read_text(product_root / "_config" / "config.toml").find("prefix = \"KAI\"") != std::string::npos, "config init did not write forced prefix");

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "admin", "sync-sequences"}) == 0, "sync-sequences failed after init");
        expect(run_command(binary, {"admin", "items", "--help"}) == 0, "admin items help failed");
        expect(run_command(binary, {"admin", "validate", "--help"}) == 0, "admin validate legacy routing failed");

        const auto text_tmp = temp_root / "backlog-text-tmp";
        std::filesystem::create_directories(text_tmp);
        set_env_var("KANO_BACKLOG_TEXT_TMP", text_tmp.string());

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Long text input smoke", "--agent", "tester"}) == 0, "workitem create failed");
        const auto long_text_item_path = product_root / "items" / "task" / "0000" / "KA-TSK-0001_long-text-input-smoke.md";
        expect(std::filesystem::exists(long_text_item_path), "workitem create did not create expected task file");

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "epic", "--title", "Parent smoke", "--agent", "tester"}) == 0, "workitem create parent epic failed");
        const auto set_parent_dry_run_output = temp_root / "admin-set-parent-dry-run.json";
        expect(run_command_capture(binary, {
            "admin", "items", "set-parent", "KA-TSK-0001",
            "--parent", "KA-EPIC-0001",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--format", "json"
        }, set_parent_dry_run_output) == 0, "admin items set-parent dry-run failed");
        expect(read_text(set_parent_dry_run_output).find("\"status\" : \"dry-run\"") != std::string::npos, "admin items set-parent dry-run did not emit dry-run status");
        expect(read_text(long_text_item_path).find("parent: KA-EPIC-0001") == std::string::npos, "admin items set-parent dry-run changed the item");

        const auto set_parent_apply_output = temp_root / "admin-set-parent-apply.json";
        expect(run_command_capture(binary, {
            "admin", "items", "set-parent", "KA-TSK-0001",
            "--parent", "KA-EPIC-0001",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--apply",
            "--format", "json"
        }, set_parent_apply_output) == 0, "admin items set-parent apply failed");
        expect(read_text(set_parent_apply_output).find("\"status\" : \"updated\"") != std::string::npos, "admin items set-parent apply did not emit updated status");
        expect(read_text(long_text_item_path).find("parent: KA-EPIC-0001") != std::string::npos, "admin items set-parent apply did not update parent");

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Trash smoke", "--agent", "tester"}) == 0, "workitem create trash target failed");
        const auto trash_item_path = product_root / "items" / "task" / "0000" / "KA-TSK-0002_trash-smoke.md";
        expect(std::filesystem::exists(trash_item_path), "workitem create did not create expected trash target file");
        const auto trash_dry_run_output = temp_root / "admin-trash-dry-run.json";
        expect(run_command_capture(binary, {
            "admin", "items", "trash", "KA-TSK-0002",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--reason", "smoke-test",
            "--format", "json"
        }, trash_dry_run_output) == 0, "admin items trash dry-run failed");
        expect(read_text(trash_dry_run_output).find("\"status\" : \"dry-run\"") != std::string::npos, "admin items trash dry-run did not emit dry-run status");
        expect(std::filesystem::exists(trash_item_path), "admin items trash dry-run moved the item");

        const auto trash_apply_output = temp_root / "admin-trash-apply.json";
        expect(run_command_capture(binary, {
            "admin", "items", "trash", "KA-TSK-0002",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--reason", "smoke-test",
            "--apply",
            "--format", "json"
        }, trash_apply_output) == 0, "admin items trash apply failed");
        expect(read_text(trash_apply_output).find("\"status\" : \"trashed\"") != std::string::npos, "admin items trash apply did not emit trashed status");
        expect(!std::filesystem::exists(trash_item_path), "admin items trash apply did not move the item");

        const auto conventions_path = product_root / "_meta" / "conventions.md";
        write_text(conventions_path, "# Conventions\n\n");
        const auto meta_dry_run_output = temp_root / "meta-ticketing-dry-run.json";
        expect(run_command_capture(binary, {
            "meta", "add-ticketing-guidance",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--model", "native-smoke",
            "--format", "json"
        }, meta_dry_run_output) == 0, "meta add-ticketing-guidance dry-run failed");
        expect(read_text(meta_dry_run_output).find("\"status\" : \"would-update\"") != std::string::npos, "meta add-ticketing-guidance dry-run did not emit would-update");
        expect(read_text(conventions_path).find("## Ticket type selection") == std::string::npos, "meta add-ticketing-guidance dry-run changed conventions");

        const auto meta_apply_output = temp_root / "meta-ticketing-apply.json";
        expect(run_command_capture(binary, {
            "meta", "add-ticketing-guidance",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--model", "native-smoke",
            "--apply",
            "--format", "json"
        }, meta_apply_output) == 0, "meta add-ticketing-guidance apply failed");
        expect(read_text(meta_apply_output).find("\"status\" : \"updated\"") != std::string::npos, "meta add-ticketing-guidance apply did not emit updated");
        expect(read_text(conventions_path).find("## Ticket type selection") != std::string::npos, "meta add-ticketing-guidance apply did not update conventions");

        const auto meta_unchanged_output = temp_root / "meta-ticketing-unchanged.json";
        expect(run_command_capture(binary, {
            "meta", "add-ticketing-guidance",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--format", "json"
        }, meta_unchanged_output) == 0, "meta add-ticketing-guidance unchanged failed");
        expect(read_text(meta_unchanged_output).find("\"status\" : \"unchanged\"") != std::string::npos, "meta add-ticketing-guidance did not emit unchanged");

        const auto context_file = text_tmp / "context.md";
        const auto goal_file = text_tmp / "goal.md";
        const auto approach_file = text_tmp / "approach.md";
        const auto acceptance_file = text_tmp / "acceptance.md";
        const auto risks_file = text_tmp / "risks.md";
        write_text(context_file, "Context line one\nContext line two\nAssumption: Native evidence records stay file-backed.\n");
        write_text(goal_file, "Goal line one\nGoal line two\n");
        write_text(approach_file, "Approach line one\nApproach line two\n");
        write_text(acceptance_file, "- Criterion one\n- Criterion two\n");
        write_text(risks_file, "Risk line one\nRisk line two\n");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "set-ready", "KA-TSK-0001",
            "--context-file", context_file.string(),
            "--goal-file", goal_file.string(),
            "--approach-file", approach_file.string(),
            "--acceptance-criteria-file", acceptance_file.string(),
            "--risks-file", risks_file.string(),
            "--agent", "tester",
            "--consume-input-files"
        }) == 0, "workitem set-ready file input failed");

        expect(!std::filesystem::exists(context_file), "set-ready did not consume context file");
        expect(!std::filesystem::exists(goal_file), "set-ready did not consume goal file");
        expect(!std::filesystem::exists(approach_file), "set-ready did not consume approach file");
        expect(!std::filesystem::exists(acceptance_file), "set-ready did not consume acceptance criteria file");
        expect(!std::filesystem::exists(risks_file), "set-ready did not consume risks file");

        std::string long_text_item = read_text(long_text_item_path);
        expect(long_text_item.find("Context line two") != std::string::npos, "set-ready did not write context file text");
        expect(long_text_item.find("- Criterion two") != std::string::npos, "set-ready did not write acceptance file text");

        const auto worklog_file = text_tmp / "worklog.md";
        write_text(worklog_file, "Worklog line one\nWorklog line two\n");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "worklog", "append", "KA-TSK-0001",
            "--message-file", worklog_file.string(),
            "--agent", "tester",
            "--consume-input-files"
        }) == 0, "worklog append file input failed");
        expect(!std::filesystem::exists(worklog_file), "worklog append did not consume message file");
        long_text_item = read_text(long_text_item_path);
        expect(long_text_item.find("Worklog line two") != std::string::npos, "worklog append did not write message file text");

        const auto artifact_source = temp_root / "artifact-note.md";
        write_text(artifact_source, "# Artifact\n\nNative attach artifact smoke.\n");
        const auto attach_artifact_output = temp_root / "attach-artifact.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "attach-artifact", "KA-TSK-0001",
            "--path", artifact_source.string(),
            "--no-shared",
            "--agent", "tester",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root-override", backlog_root.string(),
            "--note", "attached by native smoke",
            "--format", "json"
        }, attach_artifact_output) == 0, "workitem attach-artifact failed");
        expect(read_text(attach_artifact_output).find("\"worklog_appended\":true") != std::string::npos, "attach-artifact did not emit JSON worklog flag");
        expect(std::filesystem::exists(product_root / "artifacts" / "KA-TSK-0001" / "artifact-note.md"), "attach-artifact did not copy artifact to product-local store");
        long_text_item = read_text(long_text_item_path);
        expect(long_text_item.find("Artifact attached: [artifact-note.md]") != std::string::npos, "attach-artifact did not append worklog link");

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Link target smoke", "--agent", "tester"}) == 0, "workitem create link target failed");
        const auto link_target_item_path = product_root / "items" / "task" / "0000" / "KA-TSK-0003_link-target-smoke.md";
        expect(std::filesystem::exists(link_target_item_path), "workitem create did not create expected link target file");

        const auto link_source_path = product_root / "_meta" / "link-source.md";
        write_text(link_source_path,
            "# Link Source\n\n"
            "[target](KA-TSK-0003)\n"
            "[[KA-TSK-0003|Target Alias]]\n");
        const auto links_fix_dry_run = temp_root / "links-fix-dry-run.json";
        expect(run_command_capture(binary, {
            "links", "fix",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--resolve-id",
            "--format", "json"
        }, links_fix_dry_run) == 0, "links fix dry-run failed");
        expect(read_text(links_fix_dry_run).find("\"updated_files\" : 1") != std::string::npos, "links fix dry-run did not plan update");
        expect(read_text(link_source_path).find("[target](KA-TSK-0003)") != std::string::npos, "links fix dry-run modified file");

        const auto links_fix_apply = temp_root / "links-fix-apply.json";
        expect(run_command_capture(binary, {
            "links", "fix",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--resolve-id",
            "--apply",
            "--format", "json"
        }, links_fix_apply) == 0, "links fix apply failed");
        const auto fixed_link_source = read_text(link_source_path);
        expect(fixed_link_source.find("../items/task/0000/KA-TSK-0003_link-target-smoke.md") != std::string::npos, "links fix apply did not resolve ID target");
        expect(fixed_link_source.find("[[../items/task/0000/KA-TSK-0003_link-target-smoke.md|Target Alias]]") != std::string::npos, "links fix apply did not resolve wikilink target");

        const auto replace_id_path = product_root / "_meta" / "replace-id.md";
        write_text(replace_id_path,
            "# Replace ID\n\n"
            "Parent KA-TSK-0003 should change.\n\n"
            "# Worklog\n"
            "KA-TSK-0003 should stay in worklog by default.\n");
        const auto replace_id_output = temp_root / "links-replace-id.json";
        expect(run_command_capture(binary, {
            "links", "replace-id", "KA-TSK-0003", "KA-TSK-0099",
            "--path", replace_id_path.string(),
            "--apply",
            "--format", "json"
        }, replace_id_output) == 0, "links replace-id apply failed");
        const auto replace_id_text = read_text(replace_id_path);
        expect(replace_id_text.find("Parent KA-TSK-0099 should change.") != std::string::npos, "links replace-id did not update non-worklog text");
        expect(replace_id_text.find("KA-TSK-0003 should stay in worklog by default.") != std::string::npos, "links replace-id updated worklog despite default skip");

        const auto replace_target_path = product_root / "_meta" / "replace-target.md";
        write_text(replace_target_path,
            "# Replace Target\n\n"
            "[target](KA-TSK-0003)\n"
            "[[KA-TSK-0003|Target Alias]]\n");
        const auto replace_target_output = temp_root / "links-replace-target.json";
        expect(run_command_capture(binary, {
            "links", "replace-target", "KA-TSK-0003", link_target_item_path.string(),
            "--path", replace_target_path.string(),
            "--apply",
            "--format", "json"
        }, replace_target_output) == 0, "links replace-target apply failed");
        const auto replace_target_text = read_text(replace_target_path);
        expect(replace_target_text.find("../items/task/0000/KA-TSK-0003_link-target-smoke.md") != std::string::npos, "links replace-target did not write relative markdown target");
        expect(replace_target_text.find("[[KA-TSK-0003_link-target-smoke|Target Alias]]") != std::string::npos, "links replace-target did not rewrite wikilink target");

        const auto restore_probe_path = product_root / "_meta" / "restore-probe.md";
        write_text(restore_probe_path, "# Restore Probe\n\n[missing](missing-target.md)\n");
        const auto restore_probe_output = temp_root / "links-restore-from-vcs.json";
        expect(run_command_capture(binary, {
            "links", "restore-from-vcs",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, restore_probe_output) == 0, "links restore-from-vcs no-vcs probe failed");
        expect(read_text(restore_probe_output).find("\"status\" : \"no-vcs\"") != std::string::npos, "links restore-from-vcs did not report no-vcs status");

        const auto ref_remap_path = product_root / "decisions" / "ADR-0001_old-decision.md";
        write_text(ref_remap_path,
            "---\n"
            "id: ADR-0001\n"
            "title: Old Decision\n"
            "---\n\n"
            "# ADR-0001 Old Decision\n");
        const auto ref_remap_source = product_root / "_meta" / "ref-remap-source.md";
        write_text(ref_remap_source, "# Ref Remap Source\n\nSee ADR-0001 for context.\n");
        const auto ref_remap_dry_run = temp_root / "links-remap-ref-dry-run.json";
        expect(run_command_capture(binary, {
            "links", "remap-ref", ref_remap_path.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, ref_remap_dry_run) == 0, "links remap-ref dry-run failed");
        expect(read_text(ref_remap_dry_run).find("\"new_id\" : \"ADR-0002\"") != std::string::npos, "links remap-ref dry-run did not plan next ADR ID");
        expect(std::filesystem::exists(ref_remap_path), "links remap-ref dry-run removed old ADR");

        const auto ref_remap_apply = temp_root / "links-remap-ref-apply.json";
        expect(run_command_capture(binary, {
            "links", "remap-ref", ref_remap_path.string(),
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--apply",
            "--format", "json"
        }, ref_remap_apply) == 0, "links remap-ref apply failed");
        const auto ref_remap_new_path = product_root / "decisions" / "ADR-0002_old-decision.md";
        expect(std::filesystem::exists(ref_remap_new_path), "links remap-ref apply did not create new ADR path");
        expect(!std::filesystem::exists(ref_remap_path), "links remap-ref apply did not remove old ADR path");
        expect(read_text(ref_remap_new_path).find("ADR-0002 Old Decision") != std::string::npos, "links remap-ref apply did not update ADR content");
        expect(read_text(ref_remap_source).find("See ADR-0002 for context.") != std::string::npos, "links remap-ref apply did not update references");

        const auto topic_create_output = temp_root / "topic-create.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-smoke",
            "--agent", "tester",
            "--with-spec",
            "--format", "json"
        }, topic_create_output) == 0, "topic create with spec failed");
        const auto topic_path = backlog_root / "topics" / "native-topic-smoke";
        expect(std::filesystem::exists(topic_path / "manifest.json"), "topic create did not write manifest");
        expect(std::filesystem::exists(topic_path / "spec" / "requirements.md"), "topic create --with-spec did not write requirements");
        expect(read_text(topic_path / "manifest.json").find("\"has_spec\" : true") != std::string::npos, "topic create --with-spec did not mark manifest");

        const auto topic_create_list_templates_output = temp_root / "topic-create-list-templates.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-template-list-placeholder",
            "--agent", "tester",
            "--list-templates",
            "--format", "json"
        }, topic_create_list_templates_output) == 0, "topic create --list-templates failed");
        expect(read_text(topic_create_list_templates_output).find("\"feature\"") != std::string::npos, "topic create --list-templates did not include built-in templates");
        expect(!std::filesystem::exists(backlog_root / "topics" / "native-topic-template-list-placeholder"), "topic create --list-templates created a topic");

        const auto custom_template_dir = backlog_root / "_config" / "templates" / "smoke";
        write_text(custom_template_dir / "template.json",
            "{\n"
            "  \"name\": \"smoke\",\n"
            "  \"display_name\": \"Smoke Template\",\n"
            "  \"description\": \"Native custom template smoke\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"author\": \"cli_repo_smoke_test\",\n"
            "  \"tags\": [\"smoke\"],\n"
            "  \"structure\": {\n"
            "    \"directories\": [\"materials/clips\"],\n"
            "    \"files\": {\"brief.md\": \"brief.md.template\"}\n"
            "  },\n"
            "  \"manifest_defaults\": {\"status\": \"open\", \"has_spec\": false},\n"
            "  \"variables\": {\n"
            "    \"custom_title\": {\"type\": \"string\", \"description\": \"Custom title\", \"required\": true}\n"
            "  }\n"
            "}\n");
        write_text(custom_template_dir / "brief.md.template", "# {{custom_title}}\n\nNative custom template body.\n");

        const auto topic_template_list_output = temp_root / "topic-template-list.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "template", "list",
            "--format", "json"
        }, topic_template_list_output) == 0, "topic template list failed");
        expect(read_text(topic_template_list_output).find("\"builtin_count\"") != std::string::npos, "topic template list did not emit counts");
        expect(read_text(topic_template_list_output).find("\"research\"") != std::string::npos, "topic template list did not include research template");
        expect(read_text(topic_template_list_output).find("\"smoke\"") != std::string::npos, "topic template list did not include custom template");

        const auto topic_template_show_output = temp_root / "topic-template-show.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "template", "show", "research",
            "--format", "json"
        }, topic_template_show_output) == 0, "topic template show failed");
        expect(read_text(topic_template_show_output).find("\"research_question\"") != std::string::npos, "topic template show did not include variables");

        const auto topic_template_validate_output = temp_root / "topic-template-validate.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "template", "validate", "smoke",
            "--format", "json"
        }, topic_template_validate_output) == 0, "topic template validate failed");
        expect(read_text(topic_template_validate_output).find("\"valid\" : true") != std::string::npos, "topic template validate did not report valid");

        const auto topic_create_template_output = temp_root / "topic-create-template.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-template",
            "--agent", "tester",
            "--template", "smoke",
            "--var", "custom_title=Native template parity",
            "--format", "json"
        }, topic_create_template_output) == 0, "topic create --template failed");
        const auto templated_topic_path = backlog_root / "topics" / "native-topic-template";
        expect(std::filesystem::exists(templated_topic_path / "brief.md"), "topic create --template did not write brief.md");
        expect(read_text(templated_topic_path / "brief.md").find("Native template parity") != std::string::npos, "topic create --template did not substitute variables");
        expect(read_text(templated_topic_path / "manifest.json").find("\"has_spec\" : false") != std::string::npos, "topic create --template did not apply manifest defaults");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add", "native-topic-smoke",
            "--item", "KA-TSK-0001"
        }) == 0, "topic add first item failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add", "native-topic-smoke",
            "--item", "KA-TSK-0003"
        }) == 0, "topic add second item failed");
        const auto topic_manifest_after_add = read_text(topic_path / "manifest.json");
        const auto topic_seed_one = extract_json_array_string(topic_manifest_after_add, "seed_items", 0);
        const auto topic_seed_two = extract_json_array_string(topic_manifest_after_add, "seed_items", 1);

        const auto topic_doc = temp_root / "topic-doc.md";
        write_text(topic_doc, "# Topic Doc\n\nNative topic pin smoke.\n");
        const auto topic_pin_output = temp_root / "topic-pin.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "pin", "native-topic-smoke",
            "--doc", topic_doc.string(),
            "--format", "json"
        }, topic_pin_output) == 0, "topic pin failed");
        expect(read_text(topic_pin_output).find("\"pinned\" : true") != std::string::npos, "topic pin did not report pinned");
        expect(read_text(topic_path / "manifest.json").find("topic-doc.md") != std::string::npos, "topic pin did not update manifest");

        const auto topic_snippet = temp_root / "topic-snippet.cpp";
        write_text(topic_snippet,
            "#include <iostream>\n"
            "int main() {\n"
            "  return 0;\n"
            "}\n");
        const auto topic_snippet_output = temp_root / "topic-snippet.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add-snippet", "native-topic-smoke",
            "--file", topic_snippet.string(),
            "--start", "2",
            "--end", "3",
            "--agent", "tester",
            "--snapshot",
            "--format", "json"
        }, topic_snippet_output) == 0, "topic add-snippet failed");
        const auto topic_manifest_after_snippet = read_text(topic_path / "manifest.json");
        expect(topic_manifest_after_snippet.find("\"snippet_refs\"") != std::string::npos, "topic add-snippet did not write snippet_refs");
        expect(topic_manifest_after_snippet.find("sha256:") != std::string::npos, "topic add-snippet did not write sha256 hash");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-related",
            "--agent", "tester"
        }) == 0, "topic create related failed");
        const auto topic_add_ref_output = temp_root / "topic-add-reference.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add-reference", "native-topic-smoke", "native-topic-related",
            "--format", "json"
        }, topic_add_ref_output) == 0, "topic add-reference failed");
        expect(read_text(topic_add_ref_output).find("\"added\" : true") != std::string::npos, "topic add-reference did not report added");
        expect(read_text(topic_path / "manifest.json").find("native-topic-related") != std::string::npos, "topic add-reference did not update source manifest");

        const auto topic_distill_output = temp_root / "topic-distill.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "distill", "native-topic-smoke"
        }, topic_distill_output) == 0, "topic distill failed after snippet/reference");
        const auto topic_brief = read_text(topic_path / "brief.generated.md");
        expect(topic_brief.find("Snippet Refs") != std::string::npos, "topic distill did not include snippet refs section");
        expect(topic_brief.find("native-topic-related") != std::string::npos, "topic distill did not include related topic");

        const auto topic_snapshot_create_output = temp_root / "topic-snapshot-create.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "snapshot", "create", "native-topic-smoke", "before-restore",
            "--agent", "tester",
            "--description", "Snapshot smoke",
            "--format", "json"
        }, topic_snapshot_create_output) == 0, "topic snapshot create failed");
        expect(read_text(topic_snapshot_create_output).find("\"snapshot_name\":\"before-restore\"") != std::string::npos, "topic snapshot create did not emit compact JSON payload");
        expect(std::filesystem::exists(topic_path / "snapshots"), "topic snapshot create did not create snapshots directory");

        const auto topic_snapshot_list_output = temp_root / "topic-snapshot-list.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "snapshot", "list", "native-topic-smoke",
            "--format", "json"
        }, topic_snapshot_list_output) == 0, "topic snapshot list failed");
        expect(read_text(topic_snapshot_list_output).find("before-restore") != std::string::npos, "topic snapshot list did not include created snapshot");

        write_text(topic_path / "notes.md", "# Notes\n\nMutated after snapshot.\n");
        write_text(topic_path / "brief.generated.md", "# Mutated generated brief\n");
        const auto topic_snapshot_restore_output = temp_root / "topic-snapshot-restore.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "snapshot", "restore", "native-topic-smoke", "before-restore",
            "--agent", "tester",
            "--no-backup",
            "--format", "json"
        }, topic_snapshot_restore_output) == 0, "topic snapshot restore failed");
        expect(read_text(topic_snapshot_restore_output).find("\"restored_components\"") != std::string::npos, "topic snapshot restore did not emit restored components");
        expect(read_text(topic_path / "notes.md").find("Mutated after snapshot") == std::string::npos, "topic snapshot restore did not restore notes.md");
        expect(read_text(topic_path / "brief.generated.md").find("Snippet Refs") != std::string::npos, "topic snapshot restore did not restore brief.generated.md");

        const auto old_snapshot_path = topic_path / "snapshots" / "20000101T000000_old-snapshot.json";
        write_text(old_snapshot_path,
            "{\n"
            "  \"name\": \"old-snapshot\",\n"
            "  \"topic\": \"native-topic-smoke\",\n"
            "  \"created_at\": \"2000-01-01T00:00:00Z\",\n"
            "  \"created_by\": \"tester\",\n"
            "  \"description\": \"old\",\n"
            "  \"manifest\": {},\n"
            "  \"brief_content\": null,\n"
            "  \"notes_content\": null,\n"
            "  \"materials_index\": {}\n"
            "}\n");
        const auto topic_snapshot_cleanup_dry_output = temp_root / "topic-snapshot-cleanup-dry.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "snapshot", "cleanup", "native-topic-smoke",
            "--ttl-days", "1",
            "--keep-latest", "0",
            "--format", "json"
        }, topic_snapshot_cleanup_dry_output) == 0, "topic snapshot cleanup dry-run failed");
        expect(read_text(topic_snapshot_cleanup_dry_output).find("old-snapshot") != std::string::npos, "topic snapshot cleanup dry-run did not plan old snapshot");
        expect(std::filesystem::exists(old_snapshot_path), "topic snapshot cleanup dry-run deleted old snapshot");

        const auto topic_snapshot_cleanup_apply_output = temp_root / "topic-snapshot-cleanup-apply.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "snapshot", "cleanup", "native-topic-smoke",
            "--ttl-days", "1",
            "--keep-latest", "0",
            "--apply",
            "--format", "json"
        }, topic_snapshot_cleanup_apply_output) == 0, "topic snapshot cleanup apply failed");
        expect(!std::filesystem::exists(old_snapshot_path), "topic snapshot cleanup apply did not delete old snapshot");

        const auto topic_remove_ref_output = temp_root / "topic-remove-reference.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "remove-reference", "native-topic-smoke", "native-topic-related",
            "--format", "json"
        }, topic_remove_ref_output) == 0, "topic remove-reference failed");
        expect(read_text(topic_remove_ref_output).find("\"removed\" : true") != std::string::npos, "topic remove-reference did not report removed");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "switch", "native-topic-smoke",
            "--agent", "tester"
        }) == 0, "topic switch failed");
        const auto topic_state_path = backlog_root / ".cache" / "worksets" / "state.json";
        expect(read_text(topic_state_path).find("\"tester\"") != std::string::npos, "topic switch did not write shared state");
        expect(std::filesystem::exists(backlog_root / ".cache" / "worksets" / "active_topic.tester.txt"), "topic switch did not write legacy active topic file");

        const auto topic_list_active_output = temp_root / "topic-list-active.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "list-active",
            "--format", "json"
        }, topic_list_active_output) == 0, "topic list-active failed");
        expect(read_text(topic_list_active_output).find("native-topic-smoke") != std::string::npos, "topic list-active did not show switched topic");

        const auto topic_show_state_output = temp_root / "topic-show-state.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "show-state",
            "--format", "json"
        }, topic_show_state_output) == 0, "topic show-state failed");
        expect(read_text(topic_show_state_output).find("\"agents\"") != std::string::npos, "topic show-state did not emit agents");

        write_text(topic_path / "plan.md", "# Native Topic Plan\n\n- Keep the executable plan provider native.\n");
        const auto topic_resolve_opencode_output = temp_root / "topic-resolve-opencode.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "resolve-opencode-plan", "native-topic-smoke",
            "--oh-my-opencode",
            "--format", "json"
        }, topic_resolve_opencode_output) == 0, "topic resolve-opencode-plan failed");
        expect(read_text(topic_resolve_opencode_output).find("\"provider\" : \"backlog\"") != std::string::npos, "topic resolve-opencode-plan did not select backlog provider");

        const auto topic_resolve_active_output = temp_root / "topic-resolve-opencode-active.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "resolve-opencode-plan",
            "--agent", "tester",
            "--oh-my-opencode",
            "--format", "json"
        }, topic_resolve_active_output) == 0, "topic resolve-opencode-plan active fallback failed");
        expect(read_text(topic_resolve_active_output).find("\"topic\" : \"native-topic-smoke\"") != std::string::npos, "topic resolve-opencode-plan did not use active topic");

        const auto topic_resolve_compat_output = temp_root / "topic-resolve-opencode-compat.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "resolve-opencode-plan", "native-topic-smoke",
            "--oh-my-opencode",
            "--sync-compat",
            "--set-active-compat",
            "--format", "json"
        }, topic_resolve_compat_output) == 0, "topic resolve-opencode-plan compat sync failed");
        const auto sis_plan_path = temp_root / ".sisyphus" / "plans" / "native-topic-smoke.md";
        const auto sis_boulder_path = temp_root / ".sisyphus" / "boulder.json";
        expect(std::filesystem::exists(sis_plan_path), "topic resolve-opencode-plan did not write compatibility plan");
        expect(read_text(sis_boulder_path).find("\"active_plan\"") != std::string::npos, "topic resolve-opencode-plan did not update boulder active plan");

        const auto topic_sync_opencode_output = temp_root / "topic-sync-opencode.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "sync-opencode-plan", "native-topic-smoke",
            "--oh-my-opencode",
            "--set-active",
            "--format", "json"
        }, topic_sync_opencode_output) == 0, "topic sync-opencode-plan failed");
        expect(read_text(topic_sync_opencode_output).find("\"set_active\" : true") != std::string::npos, "topic sync-opencode-plan did not report set_active");
        expect(read_text(sis_plan_path).find("Keep the executable plan provider native") != std::string::npos, "topic sync-opencode-plan did not copy topic plan");

        write_text(temp_root / ".sisyphus" / "plans" / "imported-plan.md", "# Imported Plan\n\n- Imported from Sisyphus.\n");
        const auto topic_sync_import_output = temp_root / "topic-sync-opencode-import.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "sync-opencode-plan", "native-topic-smoke",
            "--oh-my-opencode",
            "--import-sisyphus-plan", "imported-plan.md",
            "--target-name", "imported-target.md",
            "--format", "json"
        }, topic_sync_import_output) == 0, "topic sync-opencode-plan import failed");
        expect(read_text(topic_path / "plan.md").find("Imported from Sisyphus") != std::string::npos, "topic sync-opencode-plan did not import source into topic plan");
        expect(read_text(temp_root / ".sisyphus" / "plans" / "imported-target.md").find("Imported from Sisyphus") != std::string::npos, "topic sync-opencode-plan did not write imported target");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-observer",
            "--agent", "tester"
        }) == 0, "topic create observer failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add-reference", "native-topic-observer", "native-topic-smoke"
        }) == 0, "topic add-reference observer failed");

        const auto topic_split_dry_output = temp_root / "topic-split-dry.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "split", "native-topic-smoke",
            "--agent", "tester",
            "--new-topic", "native-topic-split-a:" + topic_seed_one,
            "--dry-run",
            "--format", "json"
        }, topic_split_dry_output) == 0, "topic split dry-run failed");
        expect(read_text(topic_split_dry_output).find("\"dry_run\" : true") != std::string::npos, "topic split dry-run did not report dry_run");
        expect(!std::filesystem::exists(backlog_root / "topics" / "native-topic-split-a"), "topic split dry-run created target topic");

        const auto topic_split_output = temp_root / "topic-split.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "split", "native-topic-smoke",
            "--agent", "tester",
            "--new-topic", "native-topic-split-a:" + topic_seed_one,
            "--no-snapshots",
            "--format", "json"
        }, topic_split_output) == 0, "topic split apply failed");
        const auto split_topic_path = backlog_root / "topics" / "native-topic-split-a";
        expect(std::filesystem::exists(split_topic_path / "manifest.json"), "topic split did not create target topic");
        expect(read_text(split_topic_path / "manifest.json").find(topic_seed_one) != std::string::npos, "topic split target missing moved seed item");
        const auto topic_manifest_after_split = read_text(topic_path / "manifest.json");
        expect(topic_manifest_after_split.find(topic_seed_one) == std::string::npos, "topic split source still contains moved item");
        expect(topic_manifest_after_split.find(topic_seed_two) != std::string::npos, "topic split source lost untouched item");
        expect(read_text(backlog_root / "topics" / "native-topic-observer" / "manifest.json").find("native-topic-split-a") != std::string::npos, "topic split did not update related topic references");

        const auto topic_merge_dry_output = temp_root / "topic-merge-dry.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "merge", "native-topic-smoke", "native-topic-split-a",
            "--agent", "tester",
            "--dry-run",
            "--format", "json"
        }, topic_merge_dry_output) == 0, "topic merge dry-run failed");
        expect(read_text(topic_merge_dry_output).find("\"dry_run\" : true") != std::string::npos, "topic merge dry-run did not report dry_run");

        const auto topic_merge_output = temp_root / "topic-merge.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "merge", "native-topic-smoke", "native-topic-split-a",
            "--agent", "tester",
            "--no-snapshots",
            "--format", "json"
        }, topic_merge_output) == 0, "topic merge apply failed");
        expect(read_text(topic_merge_output).find("\"native-topic-split-a\"") != std::string::npos, "topic merge did not report merged source");
        expect(read_text(topic_path / "manifest.json").find(topic_seed_one) != std::string::npos, "topic merge target missing merged item");
        expect(read_text(backlog_root / "topics" / "native-topic-observer" / "manifest.json").find("native-topic-smoke") != std::string::npos, "topic merge did not rewrite related topic reference");

        write_text(backlog_root / ".cache" / "worksets" / "active_topic.legacy.txt", "native-topic-related\n");
        const auto topic_migrate_output = temp_root / "topic-migrate.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "migrate",
            "--format", "json"
        }, topic_migrate_output) == 0, "topic migrate failed");
        expect(read_text(topic_migrate_output).find("\"legacy\" : \"native-topic-related\"") != std::string::npos, "topic migrate did not migrate legacy agent");

        const auto legacy_topic_id = std::string("019ea675-1111-7000-8000-000000000001");
        const auto legacy_state_doc = backlog_root / ".cache" / "worksets" / "topics" / (legacy_topic_id + ".json");
        write_text(legacy_state_doc,
            "{\n"
            "  \"topic_id\": \"" + legacy_topic_id + "\",\n"
            "  \"name\": \"legacy-state-topic\",\n"
            "  \"participants\": [],\n"
            "  \"status\": \"active\",\n"
            "  \"created_at\": \"2026-01-01T00:00:00Z\",\n"
            "  \"updated_at\": \"2026-01-01T00:00:00Z\",\n"
            "  \"created_by\": \"tester\",\n"
            "  \"description\": \"\"\n"
            "}\n");
        const auto topic_migrate_filenames_output = temp_root / "topic-migrate-filenames.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "migrate-filenames",
            "--format", "json"
        }, topic_migrate_filenames_output) == 0, "topic migrate-filenames dry-run failed");
        expect(read_text(topic_migrate_filenames_output).find(legacy_topic_id + ".json") != std::string::npos, "topic migrate-filenames did not plan legacy file rename");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "migrate-filenames",
            "--no-dry-run"
        }) == 0, "topic migrate-filenames apply failed");
        expect(std::filesystem::exists(backlog_root / ".cache" / "worksets" / "topics" / ("legacy-state-topic_" + legacy_topic_id + ".json")), "topic migrate-filenames did not rename legacy file");

        const auto topic_cleanup_legacy_output = temp_root / "topic-cleanup-legacy.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "cleanup-legacy",
            "--format", "json"
        }, topic_cleanup_legacy_output) == 0, "topic cleanup-legacy dry-run failed");
        expect(read_text(topic_cleanup_legacy_output).find("\"dry_run\" : true") != std::string::npos, "topic cleanup-legacy did not report dry-run");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "cleanup-legacy",
            "--no-dry-run"
        }) == 0, "topic cleanup-legacy apply failed");
        expect(!std::filesystem::exists(backlog_root / ".cache" / "worksets" / "active_topic.legacy.txt"), "topic cleanup-legacy did not remove legacy active topic file");

        write_text(topic_path / "synthesis" / "decision-notes.md", "## Decisions\n\n- Keep topic manifest operations native.\n");
        const auto topic_audit_output = temp_root / "topic-decision-audit.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "decision-audit", "native-topic-smoke",
            "--format", "json"
        }, topic_audit_output) == 0, "topic decision-audit failed");
        expect(read_text(topic_audit_output).find("\"decisions_found\" : 1") != std::string::npos, "topic decision-audit did not find synthesis decision");
        expect(std::filesystem::exists(topic_path / "publish" / "decision-audit.md"), "topic decision-audit did not write report");

        const auto topic_close_output = temp_root / "topic-close.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "close", "native-topic-smoke",
            "--agent", "tester",
            "--format", "json"
        }, topic_close_output) == 0, "topic close failed");
        expect(read_text(topic_close_output).find("\"closed\" : true") != std::string::npos, "topic close did not report closed");
        expect(read_text(topic_path / "manifest.json").find("\"status\" : \"closed\"") != std::string::npos, "topic close did not update manifest status");

        const auto topic_cleanup_output = temp_root / "topic-cleanup.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "cleanup",
            "--ttl-days", "1",
            "--format", "json"
        }, topic_cleanup_output) == 0, "topic cleanup dry-run failed");
        expect(read_text(topic_cleanup_output).find("\"dry_run\" : true") != std::string::npos, "topic cleanup did not report dry-run");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "add",
            "--item", "KA-TSK-0001",
            "--claim-id", "claim-ready",
            "--source", "cli-smoke",
            "--content", "Evidence content line",
            "--evidence-id", "ev-smoke",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence add failed");
        const auto evidence_store = backlog_root / ".cache" / "worksets" / "items" / "KA-TSK-0001" / "evidence.json";
        expect(read_text(evidence_store).find("ev-smoke") != std::string::npos, "evidence add did not write evidence store");

        const auto evidence_list_output = temp_root / "evidence-list.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "list",
            "--item", "KA-TSK-0001",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, evidence_list_output) == 0, "evidence list failed");
        expect(read_text(evidence_list_output).find("\"evidence_count\"") != std::string::npos, "evidence list did not emit json summary");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "get",
            "--item", "KA-TSK-0001",
            "--evidence-id", "ev-smoke",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence get failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "summary",
            "--item", "KA-TSK-0001",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence summary failed");

        const auto assumptions_output = temp_root / "assumptions.json";
        expect(run_command_capture(binary, {
            "assumptions", "list",
            "--item", "KA-TSK-0001",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, assumptions_output) == 0, "assumptions list failed");
        expect(read_text(assumptions_output).find("Native evidence records stay file-backed") != std::string::npos, "assumptions list did not find marker text");

        const auto chunks_build_output = temp_root / "chunks-build.json";
        expect(run_command_capture(binary, {
            "chunks", "build",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--force",
            "--format", "json"
        }, chunks_build_output) == 0, "chunks build failed");
        expect(read_text(chunks_build_output).find("\"chunks_indexed\"") != std::string::npos, "chunks build did not emit json summary");

        const auto chunks_query_output = temp_root / "chunks-query.json";
        expect(run_command_capture(binary, {
            "chunks", "query", "Native",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, chunks_query_output) == 0, "chunks query failed");
        expect(read_text(chunks_query_output).find("KA-TSK-0001") != std::string::npos, "chunks query did not find native marker item");

        const auto search_output = temp_root / "search.json";
        expect(run_command_capture(binary, {
            "search", "query", "Native",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, search_output) == 0, "search query failed");
        expect(read_text(search_output).find("\"corpus\"") != std::string::npos, "search query did not emit json payload");

        const auto embedding_status_output = temp_root / "embedding-status.json";
        expect(run_command_capture(binary, {
            "embedding", "status",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, embedding_status_output) == 0, "embedding status failed");
        expect(read_text(embedding_status_output).find("\"backend_type\"") != std::string::npos, "embedding status did not emit backend type");

        const auto embedding_text_output = temp_root / "embedding-text.json";
        expect(run_command_capture(binary, {
            "embedding", "build",
            "--text", "Native heuristic embedding smoke text.",
            "--source-id", "smoke-text",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, embedding_text_output) == 0, "embedding text build failed");
        expect(read_text(embedding_text_output).find("\"chunks_count\"") != std::string::npos, "embedding text build did not emit chunk count");

        const auto embedding_query_output = temp_root / "embedding-query.json";
        expect(run_command_capture(binary, {
            "embedding", "query", "Native",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, embedding_query_output) == 0, "embedding query failed");
        expect(read_text(embedding_query_output).find("\"results\"") != std::string::npos, "embedding query did not emit results");

        const auto inspect_output = temp_root / "inspect-health.json";
        expect(run_command_capture(binary, {
            "inspect", "health",
            "--item", "KA-TSK-0001",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, inspect_output) == 0, "inspect health failed");
        expect(read_text(inspect_output).find("\"total_items_scanned\"") != std::string::npos, "inspect health did not emit scan count");

        const auto tokenizer_output = temp_root / "tokenizer.json";
        expect(run_command_capture(binary, {
            "tokenizer", "test",
            "--text", "Native tokenizer smoke text",
            "--format", "json"
        }, tokenizer_output) == 0, "tokenizer test failed");
        expect(read_text(tokenizer_output).find("\"resolved_adapter\"") != std::string::npos, "tokenizer test did not emit adapter metadata");

        const auto tokenizer_config_output = temp_root / "tokenizer-config.json";
        expect(run_command_capture(binary, {
            "tokenizer", "config",
            "--format", "json"
        }, tokenizer_config_output) == 0, "tokenizer config failed");
        expect(read_text(tokenizer_config_output).find("\"runtime\"") != std::string::npos, "tokenizer config did not emit runtime metadata");

        const auto tokenizer_example = temp_root / "tokenizer_config.toml";
        expect(run_command(binary, {
            "tokenizer", "create-example",
            "--output", tokenizer_example.string(),
            "--force"
        }) == 0, "tokenizer create-example failed");
        expect(read_text(tokenizer_example).find("adapter = \"heuristic\"") != std::string::npos, "tokenizer create-example did not write native adapter");

        const auto tokenizer_models_output = temp_root / "tokenizer-models.txt";
        expect(run_command_capture(binary, {
            "tokenizer", "list-models"
        }, tokenizer_models_output) == 0, "tokenizer list-models failed");
        expect(read_text(tokenizer_models_output).find("text-embedding-3-small") != std::string::npos, "tokenizer list-models did not include known model");

        const auto tokenizer_cache_output = temp_root / "tokenizer-cache.json";
        expect(run_command_capture(binary, {
            "tokenizer", "cache-stats",
            "--format", "json"
        }, tokenizer_cache_output) == 0, "tokenizer cache-stats failed");
        const auto tokenizer_cache_text = read_text(tokenizer_cache_output);
        expect(tokenizer_cache_text.find("\"cache_enabled\" : false") != std::string::npos, "tokenizer cache-stats did not report stateless native cache");
        expect(tokenizer_cache_text.find("placeholder") == std::string::npos, "tokenizer cache-stats still reports placeholder");

        const auto tokenizer_accuracy_output = temp_root / "tokenizer-accuracy.json";
        expect(run_command_capture(binary, {
            "tokenizer", "accuracy",
            "--format", "json"
        }, tokenizer_accuracy_output) == 0, "tokenizer accuracy failed");
        expect(read_text(tokenizer_accuracy_output).find("\"test_cases_count\"") != std::string::npos, "tokenizer accuracy did not emit sample count");

        const auto tokenizer_compare_output = temp_root / "tokenizer-compare.json";
        expect(run_command_capture(binary, {
            "tokenizer", "compare", "Native tokenizer compare smoke",
            "--adapters", "heuristic,tiktoken",
            "--format", "json"
        }, tokenizer_compare_output) == 0, "tokenizer compare failed");
        const auto tokenizer_compare_text = read_text(tokenizer_compare_output);
        expect(tokenizer_compare_text.find("\"adapter\" : \"heuristic\"") != std::string::npos, "tokenizer compare did not include heuristic adapter");
        expect(tokenizer_compare_text.find("Adapter excluded from native executable contract") != std::string::npos, "tokenizer compare did not report excluded Python adapter");

        const auto tokenizer_migrate_input = temp_root / "tokenizer-legacy.json";
        const auto tokenizer_migrate_output = temp_root / "tokenizer-legacy.native.toml";
        write_text(tokenizer_migrate_input,
            "{\n"
            "  \"adapter\": \"tiktoken\",\n"
            "  \"model\": \"gpt-4o\",\n"
            "  \"max_tokens\": 128000\n"
            "}\n");
        expect(run_command(binary, {
            "tokenizer", "migrate", tokenizer_migrate_input.string(),
            "--output", tokenizer_migrate_output.string(),
            "--force"
        }) == 0, "tokenizer migrate failed");
        const auto tokenizer_migrate_text = read_text(tokenizer_migrate_output);
        expect(tokenizer_migrate_text.find("adapter = \"heuristic\"") != std::string::npos, "tokenizer migrate did not write native adapter");
        expect(tokenizer_migrate_text.find("previous_adapter = \"tiktoken\"") != std::string::npos, "tokenizer migrate did not preserve previous adapter");

        const auto tokenizer_telemetry_output = temp_root / "tokenizer-telemetry.json";
        expect(run_command_capture(binary, {
            "tokenizer", "telemetry",
            "--format", "json"
        }, tokenizer_telemetry_output) == 0, "tokenizer telemetry failed");
        expect(read_text(tokenizer_telemetry_output).find("\"telemetry_enabled\" : false") != std::string::npos, "tokenizer telemetry did not emit native disabled status");

        const auto changelog_path = temp_root / "CHANGELOG.md";
        write_text(changelog_path,
            "# Changelog\n\n"
            "## [Unreleased]\n\n"
            "### Added\n\n"
            "- Native changelog merge smoke.\n\n"
            "## [1.0.0] - 2026-01-01\n\n"
            "- Existing release.\n");
        const auto changelog_dry_output = temp_root / "changelog-merge-dry.txt";
        expect(run_command_capture(binary, {
            "changelog", "merge-unreleased",
            "--version", "1.1.0",
            "--changelog", changelog_path.string(),
            "--date", "2026-06-08",
            "--dry-run"
        }, changelog_dry_output) == 0, "changelog merge-unreleased dry-run failed");
        expect(read_text(changelog_dry_output).find("## [1.1.0] - 2026-06-08") != std::string::npos, "changelog merge-unreleased dry-run missing version header");
        expect(read_text(changelog_path).find("## [1.1.0]") == std::string::npos, "changelog merge-unreleased dry-run wrote file");
        expect(run_command(binary, {
            "changelog", "merge-unreleased",
            "--version", "1.1.0",
            "--changelog", changelog_path.string(),
            "--date", "2026-06-08"
        }) == 0, "changelog merge-unreleased apply failed");
        const auto changelog_after_merge = read_text(changelog_path);
        expect(changelog_after_merge.find("## [1.1.0] - 2026-06-08") != std::string::npos, "changelog merge-unreleased apply missing version header");
        expect(changelog_after_merge.find("- Native changelog merge smoke.") != std::string::npos, "changelog merge-unreleased apply missing moved entry");
        expect(changelog_after_merge.find("## [Unreleased]\n\n## [1.1.0]") != std::string::npos, "changelog merge-unreleased did not clear Unreleased body");

        const auto benchmark_dir = temp_root / "benchmark-output";
        expect(run_command(binary, {
            "benchmark", "run",
            "--product", "kano-ai-3d-asset-skill",
            "--agent", "tester",
            "--corpus", (repo_root / "tests" / "fixtures" / "benchmark_corpus.json").string(),
            "--queries", (repo_root / "tests" / "fixtures" / "benchmark_queries.json").string(),
            "--out", benchmark_dir.string(),
            "--mode", "chunk-only"
        }) == 0, "benchmark run failed");
        bool benchmark_report_found = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(benchmark_dir)) {
            if (entry.is_regular_file() && entry.path().filename() == "report.json") {
                benchmark_report_found = true;
                break;
            }
        }
        expect(benchmark_report_found, "benchmark run did not write report.json");

        const auto snapshot_create_output = temp_root / "snapshot-create.md";
        expect(run_command(binary, {
            "snapshot", "create", "all",
            "--scope", "repo",
            "--write",
            "--out", snapshot_create_output.string()
        }) == 0, "snapshot create failed");
        expect(read_text(snapshot_create_output).find("Snapshot Report") != std::string::npos, "snapshot create did not write report content");

        const auto snapshot_report_output = temp_root / "snapshot-report.md";
        expect(run_command(binary, {
            "snapshot", "report", "developer",
            "--scope", "repo",
            "--write",
            "--out", snapshot_report_output.string()
        }) == 0, "snapshot report failed");
        expect(read_text(snapshot_report_output).find("Snapshot Persona Report: developer") != std::string::npos, "snapshot report did not write persona report");

        expect(run_command(binary, {
            "admin", "sandbox", "init", "native-admin-sandbox",
            "--product", "kano-ai-3d-asset-skill",
            "--agent", "tester",
            "--backlog-root", backlog_root.string(),
            "--force"
        }) == 0, "admin sandbox init failed");
        expect(std::filesystem::exists(temp_root / "_kano" / "backlog_sandbox" / "native-admin-sandbox" / "README.md"), "admin sandbox init did not create README");

        const auto persona_summary = temp_root / "persona-summary.md";
        expect(run_command(binary, {
            "admin", "persona", "summary",
            "--product", "kano-ai-3d-asset-skill",
            "--agent", "tester",
            "--backlog-root", backlog_root.string(),
            "--output", persona_summary.string()
        }) == 0, "admin persona summary failed");
        expect(read_text(persona_summary).find("Persona Activity Summary") != std::string::npos, "persona summary did not write report");

        const auto persona_report = temp_root / "persona-report.md";
        expect(run_command(binary, {
            "admin", "persona", "report",
            "--product", "kano-ai-3d-asset-skill",
            "--agent", "tester",
            "--backlog-root", backlog_root.string(),
            "--output", persona_report.string()
        }) == 0, "admin persona report failed");
        expect(read_text(persona_report).find("Items by State") != std::string::npos, "persona report did not write state breakdown");

        expect(run_command(binary, {
            "admin", "release", "check",
            "--version", "0.0.4",
            "--topic", "native-release-smoke",
            "--agent", "tester",
            "--product", "kano-ai-3d-asset-skill",
            "--sandbox", "native-release-sandbox",
            "--phase", "phase2",
            "--backlog-root", backlog_root.string()
        }) == 0, "admin release phase2 check failed");
        expect(std::filesystem::exists(backlog_root / "topics" / "native-release-smoke" / "publish" / "release_check_0.0.4_phase2.md"), "admin release phase2 did not write report");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "delete",
            "--item", "KA-TSK-0001",
            "--evidence-id", "ev-smoke",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence delete failed");
        expect(read_text(evidence_store).find("ev-smoke") == std::string::npos, "evidence delete did not remove record");

        const auto invalid_product_spaces_output = temp_root / "admin-init-invalid-product-spaces.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "tester", "--product", "bad product"}, invalid_product_spaces_output),
            invalid_product_spaces_output,
            "admin init accepted product ID with spaces",
            "Product ID must use only letters"
        );
        const auto invalid_product_dot_output = temp_root / "admin-init-invalid-product-dot.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "tester", "--product", "bad.product"}, invalid_product_dot_output),
            invalid_product_dot_output,
            "admin init accepted product ID with dot",
            "Product ID must use only letters"
        );
        const auto invalid_product_brackets_output = temp_root / "admin-init-invalid-product-brackets.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "tester", "--product", "bad[product]"}, invalid_product_brackets_output),
            invalid_product_brackets_output,
            "admin init accepted product ID with brackets",
            "Product ID must use only letters"
        );
        const auto invalid_product_path_output = temp_root / "admin-init-invalid-product-path.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "tester", "--product", "../bad"}, invalid_product_path_output),
            invalid_product_path_output,
            "admin init accepted product ID path traversal",
            "Product ID must use only letters"
        );
        const auto invalid_agent_output = temp_root / "admin-init-invalid-agent.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "bad agent", "--product", "safe-product"}, invalid_agent_output),
            invalid_agent_output,
            "admin init accepted unsafe agent ID",
            "Agent ID must use only letters"
        );

        const auto explicit_root_project = temp_root / "explicit-root-project";
        std::filesystem::create_directories(explicit_root_project);
        std::filesystem::current_path(explicit_root_project);
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "explicit-product", "--backlog-root", "_kano/backlog/products"}) == 0, "admin init failed with explicit products backlog root");
        expect(std::filesystem::exists(explicit_root_project / "_kano" / "backlog" / "products" / "explicit-product"), "explicit products backlog root was not normalized to backlog root");
        const auto invalid_backlog_root_output = explicit_root_project / "admin-init-invalid-backlog-root.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {"admin", "init", "--agent", "tester", "--product", "escape-product", "--backlog-root", "../escaped-backlog"}, invalid_backlog_root_output),
            invalid_backlog_root_output,
            "admin init accepted escaping backlog root",
            "Backlog root must stay inside the project root"
        );
        std::filesystem::current_path(temp_root);

        const auto doctor_output = temp_root / "doctor.txt";
        expect(run_command_capture(binary, {"doctor"}, doctor_output) == 0, "doctor command failed after init");
        expect(read_text(doctor_output).find("[FAIL]") == std::string::npos, "doctor reported a failure after init");

        const auto shared_layout_project = temp_root / "shared-layout-project";
        const auto shared_layout_backlog_root = shared_layout_project / "_kano" / "backlog";
        const auto shared_layout_config = shared_layout_backlog_root / ".kano" / "backlog_config.toml";
        const auto shared_layout_product = shared_layout_backlog_root / "products" / "shared-product";
        std::filesystem::create_directories(shared_layout_product / "items");
        write_text(shared_layout_config,
            "[products.shared-product]\n"
            "name = \"shared-product\"\n"
            "prefix = \"SP\"\n"
            "backlog_root = \"products/shared-product\"\n"
        );

        std::filesystem::current_path(shared_layout_project);
        const auto shared_config_show_output = temp_root / "shared-layout-config-show.txt";
        expect(run_command_capture(binary, {"-P", "shared-product", "config", "show"}, shared_config_show_output) == 0, "config show failed from shared layout workspace root");
        expect(read_text(shared_config_show_output).find("shared-product") != std::string::npos, "config show did not discover shared layout config");

        const auto shared_workspace_doctor_output = temp_root / "doctor-shared-workspace.txt";
        expect(run_command_capture(binary, {"doctor"}, shared_workspace_doctor_output) == 0, "doctor failed from shared layout workspace root");
        const auto shared_workspace_doctor_text = read_text(shared_workspace_doctor_output);
        expect(shared_workspace_doctor_text.find("[FAIL]") == std::string::npos, "doctor reported a failure from shared layout workspace root");
        expect(shared_workspace_doctor_text.find("Detected shared backlog root") != std::string::npos, "doctor did not report shared backlog root discovery");
        expect(shared_workspace_doctor_text.find(shared_layout_config.string()) != std::string::npos, "doctor did not report shared layout config path");
        expect(shared_workspace_doctor_text.find("Available products: shared-product") != std::string::npos, "doctor did not list shared layout products");
        expect(shared_workspace_doctor_text.find("Recommended: cd ") != std::string::npos, "doctor did not print recommended cd guidance");

        std::filesystem::current_path(shared_layout_backlog_root);
        const auto shared_backlog_root_doctor_output = temp_root / "doctor-shared-backlog-root.txt";
        expect(run_command_capture(binary, {"doctor"}, shared_backlog_root_doctor_output) == 0, "doctor failed from shared backlog root");
        expect(read_text(shared_backlog_root_doctor_output).find("[FAIL]") == std::string::npos, "doctor reported a failure from shared backlog root");

        std::filesystem::current_path(temp_root);
        const auto shared_explicit_doctor_output = temp_root / "doctor-shared-explicit.txt";
        expect(run_command_capture(binary, {
            "doctor",
            "--backlog-root", shared_layout_backlog_root.string(),
            "--config", shared_layout_config.string()
        }, shared_explicit_doctor_output) == 0, "doctor failed with explicit shared layout overrides");
        const auto shared_explicit_doctor_text = read_text(shared_explicit_doctor_output);
        expect(shared_explicit_doctor_text.find("[FAIL]") == std::string::npos, "doctor reported a failure with explicit shared layout overrides");
        expect(shared_explicit_doctor_text.find("Override:") != std::string::npos, "doctor did not report explicit override usage");

        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(temp_root);

        std::cout << "cli_repo_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_repo_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
