#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
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
        command += arg;
    }
    return std::system(command.c_str());
}

} // namespace

int main() {
    try {
        const std::filesystem::path repo_root(KANO_REPO_ROOT);
        const std::filesystem::path binary = repo_root / "src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog.exe";

        expect(std::filesystem::exists(binary), "native binary not found for cli_repo_smoke_test");
        std::filesystem::current_path(repo_root);

        expect(run_command(binary, {"--help"}) == 0, "help command failed");
        expect(run_command(binary, {"--version"}) == 0, "version command failed");

        std::cout << "cli_repo_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_repo_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
