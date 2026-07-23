#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::vector<std::string> with_duplicate_admission(std::vector<std::string> args, const std::string& query) {
    args.push_back("--duplicate-search-query");
    args.push_back(query);
    args.push_back("--duplicate-search-scope");
    args.push_back("kano-ai-3d-asset-skill");
    args.push_back("--duplicate-decision");
    args.push_back("create");
    return args;
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

std::filesystem::path short_path_for_test(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide_path = path.wstring();
    const DWORD required = GetShortPathNameW(wide_path.c_str(), nullptr, 0);
    if (required == 0) {
        return path;
    }

    std::wstring buffer(required, L'\0');
    const DWORD written = GetShortPathNameW(wide_path.c_str(), buffer.data(), required);
    if (written == 0 || written >= required) {
        return path;
    }
    buffer.resize(written);
    return std::filesystem::path(buffer);
#else
    return path;
#endif
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
        expect(run_command(binary, {"gui", "--help"}) == 0, "gui help failed");
        expect(run_command(binary, {"webview", "--help"}) == 0, "webview help failed");
        const auto hygiene_output = std::filesystem::temp_directory_path() / "kano-backlog-repo-hygiene-smoke.txt";
        expect_command_capture_success(
            run_command_capture(binary, {"repo-hygiene", "check", "--archive-safe"}, hygiene_output),
            hygiene_output,
            "repo-hygiene command failed"
        );
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

        const auto kfg_dry_run_output = temp_root / "admin-init-kfg-dry-run.json";
        expect_command_capture_success(
            run_command_capture(binary, {
                "admin", "init",
                "--product", "kano-forge-skill",
                "--agent", "tester",
                "--product-name", "Kano Forge Skill",
                "--prefix", " kfg ",
                "--dry-run",
                "--skip-refresh-views"
            }, kfg_dry_run_output),
            kfg_dry_run_output,
            "admin init dry-run failed"
        );
        const auto kfg_dry_run_text = read_text(kfg_dry_run_output);
        expect(kfg_dry_run_text.find("\"status\" : \"dry-run\"") != std::string::npos, "admin init dry-run did not report dry-run status");
        expect(kfg_dry_run_text.find("\"dry_run\" : true") != std::string::npos, "admin init dry-run did not report dry_run=true");
        expect(kfg_dry_run_text.find("kano-forge-skill") != std::string::npos, "admin init dry-run did not plan kano-forge-skill paths");
        expect(kfg_dry_run_text.find("KFG") != std::string::npos, "admin init dry-run did not normalize KFG prefix");
        expect(kfg_dry_run_text.find("planned_paths") != std::string::npos, "admin init dry-run did not emit planned paths");
        expect(!std::filesystem::exists(temp_root / "_kano"), "admin init dry-run created _kano");
        expect(!std::filesystem::exists(temp_root / ".kano"), "admin init dry-run created .kano");
        expect(!std::filesystem::exists(temp_root / ".gitignore"), "admin init dry-run created .gitignore");

        expect(run_command(binary, {
            "admin", "init",
            "--product", "kano-forge-skill",
            "--agent", "tester",
            "--product-name", "Kano Forge Skill",
            "--prefix", "KFG",
            "--skip-refresh-views"
        }) == 0, "admin init KFG registration failed");
        const auto kfg_product_root = temp_root / "_kano" / "backlog" / "products" / "kano-forge-skill";
        expect(std::filesystem::exists(kfg_product_root / "decisions"), "admin init KFG did not create decisions directory");
        expect(read_text(temp_root / ".kano" / "backlog_config.toml").find("prefix = \"KFG\"") != std::string::npos, "admin init KFG did not register normalized prefix");
        expect(run_command(binary, {
            "-P", "KFG",
            "workitem", "list"
        }) == 0, "registered KFG prefix did not resolve through the global product option");
        expect(run_command(binary, {
            "-P", " kfg ",
            "workitem", "list"
        }) == 0, "registered KFG prefix did not resolve case-insensitively after trimming");

        const auto kfg_collision_output = temp_root / "admin-init-kfg-collision.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "admin", "init",
                "--product", "another-forge-skill",
                "--agent", "tester",
                "--product-name", "Another Forge Skill",
                "--prefix", "KFG",
                "--skip-refresh-views"
            }, kfg_collision_output),
            kfg_collision_output,
            "admin init should reject colliding product prefix",
            "Product prefix collision"
        );
        const auto kfg_collision_text = read_text(kfg_collision_output);
        expect(kfg_collision_text.find("kano-forge-skill") != std::string::npos, "prefix collision did not list existing product");
        expect(kfg_collision_text.find("another-forge-skill") != std::string::npos, "prefix collision did not list requested product");
        expect(kfg_collision_text.find("prefix=KFG") != std::string::npos, "prefix collision did not list both prefixes");
        expect(kfg_collision_text.find("backlog_config.toml") != std::string::npos, "prefix collision did not list config path");
        expect(!std::filesystem::exists(temp_root / "_kano" / "backlog" / "products" / "another-forge-skill"), "prefix collision created the other product directory");
        expect(read_text(temp_root / ".kano" / "backlog_config.toml").find("[products.another-forge-skill]") == std::string::npos, "prefix collision registered the other product");

        expect(run_command(binary, {
            "admin", "init",
            "--product", "kano-forge-skill",
            "--agent", "tester",
            "--product-name", "Kano Forge Skill Updated",
            "--force",
            "--skip-refresh-views"
        }) == 0, "forced admin init should preserve existing explicit product prefix");
        const auto kfg_force_text = read_text(temp_root / ".kano" / "backlog_config.toml");
        expect(kfg_force_text.find("name = \"Kano Forge Skill Updated\"") != std::string::npos, "force admin init did not update product display name");
        expect(kfg_force_text.find("prefix = \"KFG\"") != std::string::npos, "force admin init did not preserve explicit KFG prefix");
        expect(kfg_force_text.find("[products.kano-forge-skill]", kfg_force_text.find("[products.kano-forge-skill]") + 1) == std::string::npos, "force admin init duplicated explicit-prefix product config block");

        for (const auto& bad_prefix : std::vector<std::string>{"K/FG", "K\\FG", "..", "K.FG", "K FG", "1FG", "KFGKFGKFGKFGKFGKFG"}) {
            const auto invalid_prefix_output = temp_root / ("admin-init-invalid-prefix-" + std::to_string(std::hash<std::string>{}(bad_prefix)) + ".txt");
            expect_command_capture_failure(
                run_command_capture(binary, {
                    "admin", "init",
                    "--product", "invalid-prefix-product",
                    "--agent", "tester",
                    "--prefix", bad_prefix,
                    "--skip-refresh-views"
                }, invalid_prefix_output),
                invalid_prefix_output,
                "admin init should reject unsafe product prefix",
                "Product prefix must be 2-16 ASCII letters or digits"
            );
            expect(!std::filesystem::exists(temp_root / "_kano" / "backlog" / "products" / "invalid-prefix-product"), "invalid prefix created product directory");
        }

        const auto derived_collision_root = temp_root / "derived-prefix-collision";
        std::filesystem::create_directories(derived_collision_root);
        std::filesystem::current_path(derived_collision_root);
        expect(run_command(binary, {
            "admin", "init",
            "--product", "kano-agent-ark-skill",
            "--agent", "tester",
            "--product-name", "Kano Agent Ark Skill",
            "--skip-refresh-views"
        }) == 0, "admin init derived KA product failed");
        const auto derived_collision_output = derived_collision_root / "admin-init-derived-collision.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "admin", "init",
                "--product", "kano-ai-3d-asset-skill",
                "--agent", "tester",
                "--product-name", "Kano AI 3D Asset Skill",
                "--skip-refresh-views"
            }, derived_collision_output),
            derived_collision_output,
            "admin init should reject derived product prefix collision",
            "Product prefix collision"
        );
        const auto derived_collision_text = read_text(derived_collision_output);
        expect(derived_collision_text.find("normalized prefix KA") != std::string::npos, "derived prefix collision did not report normalized KA prefix");
        expect(derived_collision_text.find("kano-agent-ark-skill") != std::string::npos, "derived prefix collision did not list existing product");
        expect(derived_collision_text.find("kano-ai-3d-asset-skill") != std::string::npos, "derived prefix collision did not list requested product");
        std::filesystem::current_path(temp_root);

        const auto duplicate_prefix_root = temp_root / "duplicate-prefix-config";
        std::filesystem::create_directories(duplicate_prefix_root);
        std::filesystem::current_path(duplicate_prefix_root);
        expect(run_command(binary, {
            "admin", "init",
            "--product", "duplicate-prefix-one",
            "--agent", "tester",
            "--prefix", "DUP",
            "--skip-refresh-views"
        }) == 0, "admin init first duplicate-prefix fixture failed");
        expect(run_command(binary, {
            "admin", "init",
            "--product", "duplicate-prefix-two",
            "--agent", "tester",
            "--prefix", "UNQ",
            "--skip-refresh-views"
        }) == 0, "admin init second duplicate-prefix fixture failed");
        const auto duplicate_config_path = duplicate_prefix_root / ".kano" / "backlog_config.toml";
        auto duplicate_config_text = read_text(duplicate_config_path);
        const auto unq_pos = duplicate_config_text.find("prefix = \"UNQ\"");
        expect(unq_pos != std::string::npos, "duplicate-prefix fixture missing UNQ prefix");
        duplicate_config_text.replace(unq_pos, std::string("prefix = \"UNQ\"").size(), "prefix = \"DUP\"");
        write_text(duplicate_config_path, duplicate_config_text);

        const auto duplicate_doctor_output = duplicate_prefix_root / "doctor-duplicate-prefix.txt";
        expect(run_command_capture(binary, {"doctor"}, duplicate_doctor_output) == 0, "doctor should report duplicate prefix diagnostics without crashing");
        const auto duplicate_doctor_text = read_text(duplicate_doctor_output);
        expect(duplicate_doctor_text.find("[FAIL] Product Prefix Uniqueness") != std::string::npos, "doctor did not fail duplicate prefix check");
        expect(duplicate_doctor_text.find("duplicate-prefix-one") != std::string::npos, "doctor duplicate prefix did not list first product");
        expect(duplicate_doctor_text.find("duplicate-prefix-two") != std::string::npos, "doctor duplicate prefix did not list second product");

        const auto duplicate_validate_output = duplicate_prefix_root / "config-validate-duplicate-prefix.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "config", "validate",
                "--path", duplicate_prefix_root.string(),
                "--product", "duplicate-prefix-one"
            }, duplicate_validate_output),
            duplicate_validate_output,
            "config validate should reject duplicate product prefixes",
            "Product prefix collision"
        );

        const auto duplicate_create_output = duplicate_prefix_root / "workitem-create-duplicate-prefix.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "-P", "duplicate-prefix-one",
                "workitem", "create",
                "-t", "task",
                "--title", "Ambiguous prefix smoke",
                "--agent", "tester"
            }, duplicate_create_output),
            duplicate_create_output,
            "workitem create should reject ambiguous duplicate prefixes",
            "Product prefix collision"
        );
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
        for (const std::string type : {"epic", "feature", "userstory", "task", "subtask", "bug", "issue"}) {
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

        const auto standalone_backlog_root = temp_root / "standalone-backlog";
        std::filesystem::create_directories(standalone_backlog_root / ".git");
        expect(run_command(binary, {
            "admin", "init",
            "--backlog-root", standalone_backlog_root.string(),
            "--product", "horizon-unreal-pipeline",
            "--agent", "tester",
            "--product-name", "Horizon Unreal Pipeline",
            "--prefix", "HUP",
            "--skip-refresh-views"
        }) == 0, "admin init should register products inside an independent backlog repo");
        const auto standalone_config_path = standalone_backlog_root / ".kano" / "backlog_config.toml";
        expect(std::filesystem::exists(standalone_config_path), "independent backlog repo did not get local project config");
        const std::string standalone_config_text = read_text(standalone_config_path);
        expect(standalone_config_text.find("[products.horizon-unreal-pipeline]") != std::string::npos, "independent backlog config missing HUP product");
        expect(standalone_config_text.find("prefix = \"HUP\"") != std::string::npos, "independent backlog config missing HUP prefix");
        expect(standalone_config_text.find("backlog_root = \"products/horizon-unreal-pipeline\"") != std::string::npos, "independent backlog config registered unexpected backlog root");
        expect(read_text(config_path).find("[products.horizon-unreal-pipeline]") == std::string::npos, "independent backlog registration leaked into outer project config");
        const auto standalone_config_show_output = temp_root / "standalone-config-show.txt";
        expect_command_capture_success(
            run_command_capture(binary, {
                "-p", standalone_backlog_root.string(),
                "-P", "horizon-unreal-pipeline",
                "config", "show"
            }, standalone_config_show_output),
            standalone_config_show_output,
            "independent backlog config should resolve through actual KOB create path"
        );

        const auto short_path_backlog_root = temp_root / "standalone-short-path-backlog";
        std::filesystem::create_directories(short_path_backlog_root / ".git");
        const auto short_path_arg = short_path_for_test(short_path_backlog_root);
        if (short_path_arg != short_path_backlog_root) {
            std::filesystem::current_path(short_path_backlog_root);
            expect(run_command(binary, {
                "admin", "init",
                "--backlog-root", short_path_arg.string(),
                "--product", "short-path-config-smoke",
                "--agent", "tester",
                "--product-name", "Short Path Config Smoke",
                "--prefix", "SPC",
                "--skip-refresh-views"
            }) == 0, "admin init should treat Windows short and long paths as the same independent backlog repo");
            std::filesystem::current_path(temp_root);
            const auto short_path_config = short_path_backlog_root / ".kano" / "backlog_config.toml";
            expect(std::filesystem::exists(short_path_config), "short-path independent backlog repo did not get local project config");
            expect(read_text(short_path_config).find("backlog_root = \"products/short-path-config-smoke\"") != std::string::npos, "short-path independent backlog config registered unexpected backlog root");
            expect(read_text(config_path).find("[products.short-path-config-smoke]") == std::string::npos, "short-path independent backlog registration leaked into outer project config");
        }

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

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Long text input smoke", "--agent", "tester"}, "Long text input smoke")) == 0, "workitem create failed");
        const auto long_text_item_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0001_long-text-input-smoke.md";
        expect(std::filesystem::exists(long_text_item_path), "workitem create did not create expected task file");

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "epic", "--title", "Parent smoke", "--agent", "tester"}, "Parent smoke")) == 0, "workitem create parent epic failed");
        const auto set_parent_dry_run_output = temp_root / "admin-set-parent-dry-run.json";
        expect(run_command_capture(binary, {
            "admin", "items", "set-parent", "KAI-TSK-0001",
            "--parent", "KAI-EPIC-0001",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--format", "json"
        }, set_parent_dry_run_output) == 0, "admin items set-parent dry-run failed");
        expect(read_text(set_parent_dry_run_output).find("\"status\" : \"dry-run\"") != std::string::npos, "admin items set-parent dry-run did not emit dry-run status");
        expect(read_text(long_text_item_path).find("parent: KAI-EPIC-0001") == std::string::npos, "admin items set-parent dry-run changed the item");

        const auto set_parent_apply_output = temp_root / "admin-set-parent-apply.json";
        expect(run_command_capture(binary, {
            "admin", "items", "set-parent", "KAI-TSK-0001",
            "--parent", "KAI-EPIC-0001",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester",
            "--apply",
            "--format", "json"
        }, set_parent_apply_output) == 0, "admin items set-parent apply failed");
        expect(read_text(set_parent_apply_output).find("\"status\" : \"updated\"") != std::string::npos, "admin items set-parent apply did not emit updated status");
        expect(read_text(long_text_item_path).find("parent: KAI-EPIC-0001") != std::string::npos, "admin items set-parent apply did not update parent");

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Trash smoke", "--agent", "tester"}, "Trash smoke")) == 0, "workitem create trash target failed");
        const auto trash_item_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0002_trash-smoke.md";
        expect(std::filesystem::exists(trash_item_path), "workitem create did not create expected trash target file");
        const auto trash_dry_run_output = temp_root / "admin-trash-dry-run.json";
        expect(run_command_capture(binary, {
            "admin", "items", "trash", "KAI-TSK-0002",
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
            "admin", "items", "trash", "KAI-TSK-0002",
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
            "workitem", "set-ready", "KAI-TSK-0001",
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
            "worklog", "append", "KAI-TSK-0001",
            "--message-file", worklog_file.string(),
            "--agent", "tester",
            "--consume-input-files"
        }) == 0, "worklog append file input failed");
        expect(!std::filesystem::exists(worklog_file), "worklog append did not consume message file");
        long_text_item = read_text(long_text_item_path);
        expect(long_text_item.find("Worklog line two") != std::string::npos, "worklog append did not write message file text");

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "issue", "--title", "Pre triage runtime gap", "--agent", "tester"}, "Pre triage runtime gap")) == 0, "workitem create issue failed");
        const auto issue_item_path = product_root / "items" / "issue" / "0000" / "KAI-ISS-0001_pre-triage-runtime-gap.md";
        expect(std::filesystem::exists(issue_item_path), "workitem create did not create expected issue file");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "set-ready", "KAI-ISS-0001",
            "--context", "Pre-triage issue context for an unclear runtime gap.",
            "--goal", "Capture blocker and risk evidence before deciding task or bug remediation.",
            "--approach", "Triage the report, then split follow-up implementation work once clear.",
            "--acceptance-criteria", "Issue can be created, listed, searched, moved through state, and logged.",
            "--risks", "Premature classification could hide the blocker.",
            "--agent", "tester"
        }) == 0, "issue set-ready failed");
        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "workitem", "check-ready", "KAI-ISS-0001"}) == 0, "issue check-ready failed");
        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "worklog", "append", "KAI-ISS-0001", "Issue worklog evidence", "--agent", "tester"}) == 0, "issue worklog append failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "update-state", "KAI-ISS-0001",
            "--state", "InProgress",
            "--agent", "tester",
            "--message", "Issue triage started"
        }) == 0, "issue update-state failed");
        const auto issue_list_output = temp_root / "issue-list.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "list",
            "--type", "issue"
        }, issue_list_output) == 0, "issue list failed");
        expect(read_text(issue_list_output).find("KAI-ISS-0001") != std::string::npos, "issue list did not include created issue");
        const auto issue_text = read_text(issue_item_path);
        expect(issue_text.find("type: Issue") != std::string::npos, "issue file did not preserve Issue type");
        expect(issue_text.find("state: InProgress") != std::string::npos, "issue state update did not persist");
        expect(issue_text.find("Issue worklog evidence") != std::string::npos, "issue worklog append did not persist");
        const auto issue_view_refresh_output = temp_root / "issue-view-refresh.txt";
        expect(run_command_capture(binary, {
            "view", "refresh",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--agent", "tester"
        }, issue_view_refresh_output) == 0, "view refresh after issue update failed");
        expect(read_text(issue_view_refresh_output).find("Refreshed") != std::string::npos, "view refresh did not report refreshed dashboards");

        const auto leaf_product_view_refresh_output = temp_root / "leaf-product-view-refresh.txt";
        expect(run_command_capture(binary, {
            "view", "refresh",
            "--product", "kano-ai-3d-asset-skill",
            "--agent", "tester"
        }, leaf_product_view_refresh_output) == 0, "view refresh ignored leaf --product without --backlog-root");
        expect(read_text(leaf_product_view_refresh_output).find("Refreshed") != std::string::npos, "leaf product view refresh did not report refreshed dashboards");

        const auto artifact_source = temp_root / "artifact-note.md";
        write_text(artifact_source, "# Artifact\n\nNative attach artifact smoke.\n");
        const auto attach_artifact_output = temp_root / "attach-artifact.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "attach-artifact", "KAI-TSK-0001",
            "--path", artifact_source.string(),
            "--no-shared",
            "--agent", "tester",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root-override", backlog_root.string(),
            "--note", "attached by native smoke",
            "--format", "json"
        }, attach_artifact_output) == 0, "workitem attach-artifact failed");
        expect(read_text(attach_artifact_output).find("\"worklog_appended\":true") != std::string::npos, "attach-artifact did not emit JSON worklog flag");
        expect(std::filesystem::exists(product_root / "artifacts" / "KAI-TSK-0001" / "artifact-note.md"), "attach-artifact did not copy artifact to product-local store");
        long_text_item = read_text(long_text_item_path);
        expect(long_text_item.find("Artifact attached: [artifact-note.md]") != std::string::npos, "attach-artifact did not append worklog link");

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Link target smoke", "--agent", "tester"}, "Link target smoke")) == 0, "workitem create link target failed");
        const auto link_target_item_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0003_link-target-smoke.md";
        expect(std::filesystem::exists(link_target_item_path), "workitem create did not create expected link target file");

        const auto link_source_path = product_root / "_meta" / "link-source.md";
        write_text(link_source_path,
            "# Link Source\n\n"
            "[target](KAI-TSK-0003)\n"
            "[[KAI-TSK-0003|Target Alias]]\n");
        const auto links_fix_dry_run = temp_root / "links-fix-dry-run.json";
        expect(run_command_capture(binary, {
            "links", "fix",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--resolve-id",
            "--format", "json"
        }, links_fix_dry_run) == 0, "links fix dry-run failed");
        expect(read_text(links_fix_dry_run).find("\"updated_files\" : 1") != std::string::npos, "links fix dry-run did not plan update");
        expect(read_text(link_source_path).find("[target](KAI-TSK-0003)") != std::string::npos, "links fix dry-run modified file");

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
        expect(fixed_link_source.find("../items/task/0000/KAI-TSK-0003_link-target-smoke.md") != std::string::npos, "links fix apply did not resolve ID target");
        expect(fixed_link_source.find("[[../items/task/0000/KAI-TSK-0003_link-target-smoke.md|Target Alias]]") != std::string::npos, "links fix apply did not resolve wikilink target");

        const auto replace_id_path = product_root / "_meta" / "replace-id.md";
        write_text(replace_id_path,
            "# Replace ID\n\n"
            "Parent KAI-TSK-0003 should change.\n\n"
            "# Worklog\n"
            "KAI-TSK-0003 should stay in worklog by default.\n");
        const auto replace_id_output = temp_root / "links-replace-id.json";
        expect(run_command_capture(binary, {
            "links", "replace-id", "KAI-TSK-0003", "KAI-TSK-0099",
            "--path", replace_id_path.string(),
            "--apply",
            "--format", "json"
        }, replace_id_output) == 0, "links replace-id apply failed");
        const auto replace_id_text = read_text(replace_id_path);
        expect(replace_id_text.find("Parent KAI-TSK-0099 should change.") != std::string::npos, "links replace-id did not update non-worklog text");
        expect(replace_id_text.find("KAI-TSK-0003 should stay in worklog by default.") != std::string::npos, "links replace-id updated worklog despite default skip");

        const auto replace_target_path = product_root / "_meta" / "replace-target.md";
        write_text(replace_target_path,
            "# Replace Target\n\n"
            "[target](KAI-TSK-0003)\n"
            "[[KAI-TSK-0003|Target Alias]]\n");
        const auto replace_target_output = temp_root / "links-replace-target.json";
        expect(run_command_capture(binary, {
            "links", "replace-target", "KAI-TSK-0003", link_target_item_path.string(),
            "--path", replace_target_path.string(),
            "--apply",
            "--format", "json"
        }, replace_target_output) == 0, "links replace-target apply failed");
        const auto replace_target_text = read_text(replace_target_path);
        expect(replace_target_text.find("../items/task/0000/KAI-TSK-0003_link-target-smoke.md") != std::string::npos, "links replace-target did not write relative markdown target");
        expect(replace_target_text.find("[[KAI-TSK-0003_link-target-smoke|Target Alias]]") != std::string::npos, "links replace-target did not rewrite wikilink target");

        expect(run_command(binary, with_duplicate_admission({"-P", "kano-ai-3d-asset-skill", "workitem", "create", "-t", "task", "--title", "Remap smoke", "--agent", "tester"}, "Remap smoke")) == 0, "workitem create remap target failed");
        const auto remap_old_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0004_remap-smoke.md";
        const auto remap_new_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0044_remap-smoke.md";
        const auto remap_old_index_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0004_remap-smoke.index.md";
        const auto remap_new_index_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0044_remap-smoke.index.md";
        expect(std::filesystem::exists(remap_old_path), "workitem create did not create expected remap source file");
        write_text(remap_old_index_path, "# Remap index\n\nID: KAI-TSK-0004\n");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "update-state", "KAI-TSK-0004",
            "--state", "InProgress",
            "--agent", "tester",
            "--force",
            "--message", "Remap smoke state preservation"
        }) == 0, "remap source update-state failed");
        const auto remap_before_text = read_text(remap_old_path);
        const auto uid_pos = remap_before_text.find("uid: ");
        expect(uid_pos != std::string::npos, "remap source missing uid");
        const auto uid_end = remap_before_text.find('\n', uid_pos);
        const auto remap_uid_line = remap_before_text.substr(uid_pos, uid_end - uid_pos);
        const auto remap_ref_path = product_root / "_meta" / "remap-id-source.md";
        write_text(remap_ref_path, "# Remap ID Source\n\nReferences KAI-TSK-0004 before remap.\n");

        const auto remap_dry_run_output = temp_root / "workitem-remap-id-dry-run.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "remap-id", "KAI-TSK-0004",
            "--to", "KAI-TSK-0044",
            "--agent", "tester",
            "--format", "json"
        }, remap_dry_run_output) == 0, "workitem remap-id dry-run failed");
        const auto remap_dry_run_text = read_text(remap_dry_run_output);
        expect(remap_dry_run_text.find("\"status\" : \"dry-run\"") != std::string::npos, "workitem remap-id dry-run did not emit dry-run status");
        expect(remap_dry_run_text.find("\"new_id\" : \"KAI-TSK-0044\"") != std::string::npos, "workitem remap-id dry-run did not emit new id");
        expect(remap_dry_run_text.find("planned_updated_files") != std::string::npos, "workitem remap-id dry-run did not emit planned files");
        expect(remap_dry_run_text.find("old_index_path") != std::string::npos &&
               remap_dry_run_text.find("new_index_path") != std::string::npos,
            "workitem remap-id dry-run did not expose adjacent index rename");
        expect(std::filesystem::exists(remap_old_path), "workitem remap-id dry-run removed old path");
        expect(!std::filesystem::exists(remap_new_path), "workitem remap-id dry-run created new path");
        expect(std::filesystem::exists(remap_old_index_path) && !std::filesystem::exists(remap_new_index_path),
            "workitem remap-id dry-run mutated adjacent index path");
        expect(read_text(remap_ref_path).find("KAI-TSK-0004") != std::string::npos, "workitem remap-id dry-run updated references");

        const auto links_remap_dry_run_output = temp_root / "links-remap-id-dry-run.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "links", "remap-id", "KAI-TSK-0004",
            "--to", "KAI-TSK-0044",
            "--agent", "tester",
            "--format", "json"
        }, links_remap_dry_run_output) == 0, "links remap-id dry-run failed");
        expect(read_text(links_remap_dry_run_output).find("\"status\" : \"dry-run\"") != std::string::npos, "links remap-id dry-run did not emit dry-run status");
        expect(std::filesystem::exists(remap_old_path), "links remap-id dry-run removed old path");

        const auto links_remap_apply_output = temp_root / "links-remap-id-apply.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "links", "remap-id", "KAI-TSK-0004",
            "--to", "KAI-TSK-0044",
            "--agent", "tester",
            "--apply",
            "--format", "json"
        }, links_remap_apply_output) == 0, "links remap-id apply failed");
        const auto links_remap_apply_text = read_text(links_remap_apply_output);
        expect(links_remap_apply_text.find("\"status\" : \"applied\"") != std::string::npos, "links remap-id apply did not emit applied status");
        expect(!std::filesystem::exists(remap_old_path), "links remap-id apply left old path");
        expect(std::filesystem::exists(remap_new_path), "links remap-id apply did not create new path");
        expect(!std::filesystem::exists(remap_old_index_path) && std::filesystem::exists(remap_new_index_path),
            "links remap-id apply did not rename adjacent index path");
        expect(read_text(remap_new_index_path).find("KAI-TSK-0044") != std::string::npos,
            "links remap-id apply did not update adjacent index content");
        const auto remap_after_text = read_text(remap_new_path);
        expect(remap_after_text.find("id: KAI-TSK-0044") != std::string::npos, "links remap-id apply did not update frontmatter id");
        expect(remap_after_text.find(remap_uid_line) != std::string::npos, "links remap-id apply did not preserve uid");
        expect(remap_after_text.find("state: InProgress") != std::string::npos, "links remap-id apply did not preserve state");
        expect(remap_after_text.find("Remapped ID: KAI-TSK-0004 -> KAI-TSK-0044") != std::string::npos, "links remap-id apply did not append worklog evidence");
        expect(read_text(remap_ref_path).find("KAI-TSK-0044") != std::string::npos, "links remap-id apply did not update references");
        expect(read_text(remap_ref_path).find("KAI-TSK-0004") == std::string::npos, "links remap-id apply left stale old reference");

        const auto remap_default_path = product_root / "items" / "task" / "0000" / "KAI-TSK-0045_remap-smoke.md";
        const auto remap_default_output = temp_root / "workitem-remap-id-default-format.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "workitem", "remap-id", "KAI-TSK-0044",
            "--to", "KAI-TSK-0045",
            "--agent", "tester",
            "--apply"
        }, remap_default_output) == 0, "workitem remap-id omitted-format apply failed");
        expect(read_text(remap_default_output).find("Remapped ID: KAI-TSK-0044 -> KAI-TSK-0045") != std::string::npos,
               "workitem remap-id omitted-format apply did not use plain output");
        expect(!std::filesystem::exists(remap_new_path), "workitem remap-id omitted-format apply left old path");
        expect(std::filesystem::exists(remap_default_path), "workitem remap-id omitted-format apply did not create new path");
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
        expect(read_text(topic_create_output).find("Warning: Topic name 'native-topic-smoke' is missing YYYY-MM-DD- prefix") != std::string::npos,
               "topic create did not warn for legacy non-prefixed topic by default");
        const auto topic_path = backlog_root / "topics" / "native-topic-smoke";
        expect(std::filesystem::exists(topic_path / "manifest.json"), "topic create did not write manifest");
        expect(std::filesystem::exists(topic_path / "spec" / "requirements.md"), "topic create --with-spec did not write requirements");
        expect(read_text(topic_path / "manifest.json").find("\"has_spec\" : true") != std::string::npos, "topic create --with-spec did not mark manifest");

        const auto topic_create_prefixed_output = temp_root / "topic-create-prefixed.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "2026-02-01-native-prefixed",
            "--agent", "tester",
            "--format", "json"
        }, topic_create_prefixed_output) == 0, "topic create date-prefixed failed");
        expect(read_text(topic_create_prefixed_output).find("Warning: Topic name") == std::string::npos,
               "topic create warned for date-prefixed topic");

        const auto product_toml_config = product_root / "_config" / "config.toml";
        write_text(product_toml_config,
            "[product]\n"
            "name = \"kano-ai-3d-asset-skill\"\n"
            "prefix = \"KAI\"\n"
            "topics_date_prefix_policy = \"enforce\"\n");
        const auto topic_create_enforce_output = temp_root / "topic-create-enforce.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "-P", "kano-ai-3d-asset-skill",
                "topic", "create", "enforced-legacy-topic",
                "--agent", "tester"
            }, topic_create_enforce_output),
            topic_create_enforce_output,
            "topic create enforce should reject missing date prefix",
            "missing YYYY-MM-DD- prefix"
        );
        expect(!std::filesystem::exists(backlog_root / "topics" / "enforced-legacy-topic"),
               "topic create enforce created a rejected topic directory");

        const auto topic_create_enforce_prefixed_output = temp_root / "topic-create-enforce-prefixed.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "2026-02-02-enforced-prefixed",
            "--agent", "tester"
        }, topic_create_enforce_prefixed_output) == 0, "topic create enforce rejected date-prefixed topic");

        write_text(product_toml_config,
            "[product]\n"
            "name = \"kano-ai-3d-asset-skill\"\n"
            "prefix = \"KAI\"\n"
            "topics_date_prefix_policy = \"off\"\n");
        const auto topic_create_off_output = temp_root / "topic-create-off.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "legacy-off-topic",
            "--agent", "tester"
        }, topic_create_off_output) == 0, "topic create policy off failed");
        expect(read_text(topic_create_off_output).find("Warning: Topic name") == std::string::npos,
               "topic create policy off emitted date-prefix warning");

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

        write_text(product_toml_config,
            "[product]\n"
            "name = \"kano-ai-3d-asset-skill\"\n"
            "prefix = \"KAI\"\n"
            "topics_date_prefix_policy = \"enforce\"\n");
        const auto topic_create_template_enforce_output = temp_root / "topic-create-template-enforce.txt";
        expect_command_capture_failure(
            run_command_capture(binary, {
                "-P", "kano-ai-3d-asset-skill",
                "topic", "create", "templated-enforced-legacy",
                "--agent", "tester",
                "--template", "smoke",
                "--var", "custom_title=Rejected template topic"
            }, topic_create_template_enforce_output),
            topic_create_template_enforce_output,
            "topic create --template enforce should reject missing date prefix",
            "missing YYYY-MM-DD- prefix"
        );
        expect(!std::filesystem::exists(backlog_root / "topics" / "templated-enforced-legacy"),
               "topic create --template enforce created a rejected topic directory");

        const auto topic_create_template_prefixed_output = temp_root / "topic-create-template-prefixed.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "2026-02-03-templated-prefixed",
            "--agent", "tester",
            "--template", "smoke",
            "--var", "custom_title=Accepted template topic",
            "--format", "json"
        }, topic_create_template_prefixed_output) == 0, "topic create --template date-prefixed failed under enforce policy");
        expect(read_text(topic_create_template_prefixed_output).find("Warning: Topic name") == std::string::npos,
               "topic create --template warned for date-prefixed topic");

        write_text(product_toml_config,
            "[product]\n"
            "name = \"kano-ai-3d-asset-skill\"\n"
            "prefix = \"KAI\"\n"
            "topics_date_prefix_policy = \"off\"\n");

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
            "--item", "KAI-TSK-0001"
        }) == 0, "topic add first item failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "add", "native-topic-smoke",
            "--item", "KAI-TSK-0003"
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

        const auto cli_audit_closed_materials = backlog_root / "topics" / "2026-01-05-cli-audit-closed-materials";
        write_text(cli_audit_closed_materials / "manifest.json",
            "{\n"
            "  \"topic\": \"2026-01-05-cli-audit-closed-materials\",\n"
            "  \"agent\": \"tester\",\n"
            "  \"created_at\": \"2026-01-05T00:00:00Z\",\n"
            "  \"updated_at\": \"2026-01-05T00:00:00Z\",\n"
            "  \"status\": \"closed\",\n"
            "  \"closed_at\": \"2026-01-05T00:00:00Z\",\n"
            "  \"seed_items\": [],\n"
            "  \"pinned_docs\": [],\n"
            "  \"snippet_refs\": [],\n"
            "  \"related_topics\": [],\n"
            "  \"has_spec\": false\n"
            "}\n");
        write_text(cli_audit_closed_materials / "materials" / "clips" / "old.txt", "old audit material\n");
        const auto cli_audit_closed_empty = backlog_root / "topics" / "2026-01-06-cli-audit-closed-empty";
        write_text(cli_audit_closed_empty / "manifest.json",
            "{\n"
            "  \"topic\": \"2026-01-06-cli-audit-closed-empty\",\n"
            "  \"agent\": \"tester\",\n"
            "  \"created_at\": \"2026-01-06T00:00:00Z\",\n"
            "  \"updated_at\": \"2026-01-06T00:00:00Z\",\n"
            "  \"status\": \"closed\",\n"
            "  \"closed_at\": \"2026-01-06T00:00:00Z\",\n"
            "  \"seed_items\": [],\n"
            "  \"pinned_docs\": [],\n"
            "  \"snippet_refs\": [],\n"
            "  \"related_topics\": [],\n"
            "  \"has_spec\": false\n"
            "}\n");
        const auto cli_audit_closed_manifest_before = read_text(cli_audit_closed_materials / "manifest.json");

        const auto topic_audit_json_output = temp_root / "topic-audit.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "audit",
            "--ttl-days", "14",
            "--stale-days", "30",
            "--as-of", "2026-03-01",
            "--format", "json"
        }, topic_audit_json_output) == 0, "topic audit json failed");
        const auto topic_audit_json = read_text(topic_audit_json_output);
        expect(topic_audit_json.find("\"mutated\" : false") != std::string::npos, "topic audit json did not report mutated=false");
        expect(topic_audit_json.find("\"age_days\"") != std::string::npos, "topic audit json missing age_days");
        expect(topic_audit_json.find("\"inactive_days\"") != std::string::npos, "topic audit json missing inactive_days");
        expect(topic_audit_json.find("\"active_agents\"") != std::string::npos, "topic audit json missing active_agents");
        expect(topic_audit_json.find("cleanup_materials_candidate") != std::string::npos, "topic audit json missing cleanup candidate");
        expect(topic_audit_json.find("delete_topic_candidate") != std::string::npos, "topic audit json missing delete candidate");
        expect(topic_audit_json.find("missing_date_prefix") != std::string::npos, "topic audit json missing date-prefix reason");
        expect(read_text(cli_audit_closed_materials / "manifest.json") == cli_audit_closed_manifest_before,
               "topic audit mutated closed topic manifest");
        expect(!std::filesystem::exists(cli_audit_closed_materials / "publish" / "topic-audit.md"),
               "topic audit wrote a report file");

        const auto topic_audit_markdown_output = temp_root / "topic-audit.md";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "audit",
            "--ttl-days", "14",
            "--stale-days", "30",
            "--as-of", "2026-03-01",
            "--format", "markdown"
        }, topic_audit_markdown_output) == 0, "topic audit markdown failed");
        expect(read_text(topic_audit_markdown_output).find("# Topic Audit") != std::string::npos,
               "topic audit markdown missing heading");

        const auto topic_audit_plain_output = temp_root / "topic-audit.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "audit",
            "--ttl-days", "14",
            "--stale-days", "30",
            "--as-of", "2026-03-01",
            "--format", "plain"
        }, topic_audit_plain_output) == 0, "topic audit plain failed");
        expect(read_text(topic_audit_plain_output).find("Topic audit as of 2026-03-01") != std::string::npos,
               "topic audit plain missing heading");

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

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-close-default",
            "--agent", "tester"
        }) == 0, "topic create for default close format failed");
        const auto topic_close_default_output = temp_root / "topic-close-default.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "close", "native-topic-close-default",
            "--agent", "tester"
        }, topic_close_default_output) == 0, "topic close without format failed");
        expect(read_text(topic_close_default_output).find("Closed topic 'native-topic-close-default'") != std::string::npos,
            "topic close without format did not default to plain output");
        expect(read_text(backlog_root / "topics" / "native-topic-close-default" / "manifest.json").find("\"status\" : \"closed\"") != std::string::npos,
            "topic close without format did not update manifest status");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-close-explicit-plain",
            "--agent", "tester"
        }) == 0, "topic create for explicit plain close format failed");
        const auto topic_close_plain_output = temp_root / "topic-close-plain.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "close", "native-topic-close-explicit-plain",
            "--agent", "tester",
            "--format", "plain"
        }, topic_close_plain_output) == 0, "topic close with explicit plain format failed");
        expect(read_text(topic_close_plain_output).find("Closed topic 'native-topic-close-explicit-plain'") != std::string::npos,
            "topic close with explicit plain format did not emit plain output");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "create", "native-topic-close-invalid",
            "--agent", "tester"
        }) == 0, "topic create for invalid close format failed");
        const auto topic_close_invalid_output = temp_root / "topic-close-invalid.txt";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "close", "native-topic-close-invalid",
            "--agent", "tester",
            "--format", "yaml"
        }, topic_close_invalid_output) != 0, "topic close accepted an invalid format");
        expect(read_text(backlog_root / "topics" / "native-topic-close-invalid" / "manifest.json").find("\"status\" : \"open\"") != std::string::npos,
            "topic close mutated the manifest after invalid format rejection");

        const auto topic_close_output = temp_root / "topic-close.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "close", "native-topic-smoke",
            "--agent", "tester",
            "--format", "json"
        }, topic_close_output) == 0, "topic close failed");
        expect(read_text(topic_close_output).find("\"closed\" : true") != std::string::npos, "topic close did not report closed");
        expect(read_text(topic_path / "manifest.json").find("\"status\" : \"closed\"") != std::string::npos, "topic close did not update manifest status");

        auto active_closed_manifest = read_text(topic_path / "manifest.json");
        auto closed_at_pos = active_closed_manifest.find("\"closed_at\" : \"");
        expect(closed_at_pos != std::string::npos, "closed active topic manifest missing closed_at");
        closed_at_pos += std::string("\"closed_at\" : \"").size();
        const auto closed_at_end = active_closed_manifest.find('"', closed_at_pos);
        expect(closed_at_end != std::string::npos, "closed active topic manifest has malformed closed_at");
        active_closed_manifest.replace(closed_at_pos, closed_at_end - closed_at_pos, "2026-01-01T00:00:00Z");
        write_text(topic_path / "manifest.json", active_closed_manifest);

        const auto topic_cleanup_output = temp_root / "topic-cleanup.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "topic", "cleanup",
            "--ttl-days", "1",
            "--format", "json"
        }, topic_cleanup_output) == 0, "topic cleanup dry-run failed");
        const auto topic_cleanup_text = read_text(topic_cleanup_output);
        expect(topic_cleanup_text.find("\"dry_run\" : true") != std::string::npos, "topic cleanup did not report dry-run");
        expect(topic_cleanup_text.find("native-topic-smoke") == std::string::npos, "topic cleanup planned deletion for active topic");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "add",
            "--item", "KAI-TSK-0001",
            "--claim-id", "claim-ready",
            "--source", "cli-smoke",
            "--content", "Evidence content line",
            "--evidence-id", "ev-smoke",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence add failed");
        const auto evidence_store = backlog_root / ".cache" / "worksets" / "items" / "KAI-TSK-0001" / "evidence.json";
        expect(read_text(evidence_store).find("ev-smoke") != std::string::npos, "evidence add did not write evidence store");

        const auto evidence_list_output = temp_root / "evidence-list.json";
        expect(run_command_capture(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "list",
            "--item", "KAI-TSK-0001",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, evidence_list_output) == 0, "evidence list failed");
        expect(read_text(evidence_list_output).find("\"evidence_count\"") != std::string::npos, "evidence list did not emit json summary");

        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "get",
            "--item", "KAI-TSK-0001",
            "--evidence-id", "ev-smoke",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence get failed");
        expect(run_command(binary, {
            "-P", "kano-ai-3d-asset-skill",
            "evidence", "summary",
            "--item", "KAI-TSK-0001",
            "--backlog-root", backlog_root.string()
        }) == 0, "evidence summary failed");

        const auto assumptions_output = temp_root / "assumptions.json";
        expect(run_command_capture(binary, {
            "assumptions", "list",
            "--item", "KAI-TSK-0001",
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
        expect(read_text(chunks_query_output).find("KAI-TSK-0001") != std::string::npos, "chunks query did not find native marker item");

        const auto search_output = temp_root / "search.json";
        expect(run_command_capture(binary, {
            "search", "query", "Native",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, search_output) == 0, "search query failed");
        expect(read_text(search_output).find("\"corpus\"") != std::string::npos, "search query did not emit json payload");

        const auto issue_search_output = temp_root / "issue-search.json";
        expect(run_command_capture(binary, {
            "search", "query", "pre-triage",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, issue_search_output) == 0, "issue search query failed");
        expect(read_text(issue_search_output).find("KAI-ISS-0001") != std::string::npos, "issue search did not find created issue");

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
            "--item", "KAI-TSK-0001",
            "--backlog-root", backlog_root.string(),
            "--format", "json"
        }, inspect_output) == 0, "inspect health failed");
        const auto inspect_health_text = read_text(inspect_output);
        expect(inspect_health_text.find("\"total_items_scanned\"") != std::string::npos, "inspect health did not emit scan count");
        expect(inspect_health_text.find("\"total_items_scanned\" : 1") != std::string::npos,
               "inspect health did not retain its explicit item filter");

        const auto integrity_output = temp_root / "inspect-integrity.json";
        expect(run_command_capture(binary, {
            "inspect", "integrity",
            "--product", "kano-ai-3d-asset-skill",
            "--backlog-root", backlog_root.string(),
            "--format", "json",
            "--as-of", "2026-07-23",
            "--stale-days", "90"
        }, integrity_output) == 0, "inspect integrity failed");
        const auto integrity_text = read_text(integrity_output);
        expect(integrity_text.find("\"products_scanned\"") != std::string::npos,
               "inspect integrity did not emit scanned products");
        expect(integrity_text.find("kano-ai-3d-asset-skill") != std::string::npos,
               "inspect integrity did not retain its explicit product");
        expect(integrity_text.find("\"as_of\" : \"2026-07-23\"") != std::string::npos,
               "inspect integrity did not retain its explicit as-of date");

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
            "--item", "KAI-TSK-0001",
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
