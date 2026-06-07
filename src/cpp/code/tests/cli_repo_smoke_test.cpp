#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int run_command(const std::filesystem::path& binary, const std::vector<std::string>& args) {
    std::string command = "cd /d \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
    for (const auto& arg : args) {
        command += " ";
        command += "\"";
        command += arg;
        command += "\"";
    }
    return std::system(command.c_str());
}

int run_command_capture(const std::filesystem::path& binary, const std::vector<std::string>& args, const std::filesystem::path& output_path) {
    std::string command = "cd /d \"" + std::filesystem::current_path().string() + "\" && \"" + binary.string() + "\"";
    for (const auto& arg : args) {
        command += " ";
        command += "\"";
        command += arg;
        command += "\"";
    }
    command += " > \"" + output_path.string() + "\" 2>&1";
    return std::system(command.c_str());
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

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path repo_root(KANO_REPO_ROOT);
        const std::filesystem::path executable_path =
            (argc > 0 && argv != nullptr) ? std::filesystem::absolute(argv[0]) : std::filesystem::path();
        std::filesystem::path binary = executable_path.parent_path() / "kano-backlog.exe";
        if (!std::filesystem::exists(binary)) {
            binary = repo_root / "src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog.exe";
        }

        expect(std::filesystem::exists(binary), "native binary not found for cli_repo_smoke_test");
        if (std::filesystem::exists(repo_root)) {
            std::filesystem::current_path(repo_root);
        } else {
            std::filesystem::current_path(binary.parent_path());
        }

        expect(run_command(binary, {"--help"}) == 0, "help command failed");
        expect(run_command(binary, {"--version"}) == 0, "version command failed");

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path() / ("kano-backlog-cli-init-smoke-" + std::to_string(unique));
        std::filesystem::create_directories(temp_root);

        const auto original_cwd = std::filesystem::current_path();
        std::filesystem::current_path(temp_root);

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "admin", "init", "--agent", "tester"}) == 0, "admin init command failed");
        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "admin", "init", "--agent", "tester"}) != 0, "duplicate admin init should fail without --force");
        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "admin", "init", "--agent", "tester", "--product-name", "Kano AI 3D Asset Skill", "--force"}) == 0, "forced admin init with spaced product name failed");

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
        expect(run_command_capture(binary, {"-P", "kano-ai-3d-asset-skill", "config", "show"}, config_show_output) == 0, "config show failed after spaced product-name init");
        expect(read_text(config_show_output).find("Kano AI 3D Asset Skill") != std::string::npos, "config show did not reload spaced product name");

        expect(run_command(binary, {"-P", "kano-ai-3d-asset-skill", "admin", "sync-sequences"}) == 0, "sync-sequences failed after init");

        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "bad product"}) != 0, "admin init accepted product ID with spaces");
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "bad.product"}) != 0, "admin init accepted product ID with dot");
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "bad[product]"}) != 0, "admin init accepted product ID with brackets");
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "../bad"}) != 0, "admin init accepted product ID path traversal");
        expect(run_command(binary, {"admin", "init", "--agent", "bad agent", "--product", "safe-product"}) != 0, "admin init accepted unsafe agent ID");

        const auto explicit_root_project = temp_root / "explicit-root-project";
        std::filesystem::create_directories(explicit_root_project);
        std::filesystem::current_path(explicit_root_project);
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "explicit-product", "--backlog-root", "_kano/backlog/products"}) == 0, "admin init failed with explicit products backlog root");
        expect(std::filesystem::exists(explicit_root_project / "_kano" / "backlog" / "products" / "explicit-product"), "explicit products backlog root was not normalized to backlog root");
        expect(run_command(binary, {"admin", "init", "--agent", "tester", "--product", "escape-product", "--backlog-root", "../escaped-backlog"}) != 0, "admin init accepted escaping backlog root");
        std::filesystem::current_path(temp_root);

        const auto doctor_output = temp_root / "doctor.txt";
        expect(run_command_capture(binary, {"doctor"}, doctor_output) == 0, "doctor command failed after init");
        expect(read_text(doctor_output).find("[FAIL]") == std::string::npos, "doctor reported a failure after init");

        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(temp_root);

        std::cout << "cli_repo_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_repo_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
