#pragma once
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace kano::backlog::adapters {

class FilesystemAdapter {
public:
    virtual ~FilesystemAdapter() = default;
    
    virtual std::optional<std::string> read_file(const std::filesystem::path& path) = 0;
    virtual bool write_file(const std::filesystem::path& path, const std::string& content) = 0;
    virtual bool append_file(const std::filesystem::path& path, const std::string& content) = 0;
    virtual std::vector<std::filesystem::path> list_items(const std::filesystem::path& backlog_root) = 0;
    virtual bool exists(const std::filesystem::path& path) = 0;
};

class LocalFilesystem : public FilesystemAdapter {
public:
    std::optional<std::string> read_file(const std::filesystem::path& path) override;
    bool write_file(const std::filesystem::path& path, const std::string& content) override;
    bool append_file(const std::filesystem::path& path, const std::string& content) override;
    std::vector<std::filesystem::path> list_items(const std::filesystem::path& backlog_root) override;
    bool exists(const std::filesystem::path& path) override;
};

} // namespace kano::backlog::adapters
