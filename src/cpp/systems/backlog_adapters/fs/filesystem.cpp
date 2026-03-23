#include "filesystem.hpp"
#include <fstream>
#include <sstream>

namespace kano::backlog::adapters {

std::optional<std::string> LocalFilesystem::read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool LocalFilesystem::write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) return false;
    file << content;
    return file.good();
}

bool LocalFilesystem::append_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::app);
    if (!file) return false;
    file << content;
    return file.good();
}

std::vector<std::filesystem::path> LocalFilesystem::list_items(const std::filesystem::path& backlog_root) {
    std::vector<std::filesystem::path> items;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(backlog_root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            items.push_back(entry.path());
        }
    }
    return items;
}

bool LocalFilesystem::exists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

} // namespace kano::backlog::adapters
