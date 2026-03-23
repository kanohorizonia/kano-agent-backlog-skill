#pragma once
#include "../../backlog_adapters/fs/filesystem.hpp"
#include "../../backlog_core/model/backlog_item.hpp"
#include <memory>
#include <filesystem>

namespace kano::backlog::ops {

class WorkitemOps {
public:
    WorkitemOps(std::shared_ptr<adapters::FilesystemAdapter> fs, std::filesystem::path product_root);
    
    core::BacklogItem create_item(
        core::ItemType type,
        const std::string& title,
        const std::string& product,
        const std::string& agent);
    bool update_state(const std::string& item_id, core::ItemState new_state);
    std::optional<core::BacklogItem> get_item(const std::string& item_id);
    std::vector<core::BacklogItem> list_items();
    bool append_worklog(
        const std::string& item_id,
        const std::string& message,
        const std::string& agent);
    bool add_decision(
        const std::string& item_id,
        const std::string& decision,
        const std::optional<std::string>& source,
        const std::string& agent);
    bool attach_artifact(
        const std::string& item_id,
        const std::filesystem::path& source_path,
        bool shared,
        const std::optional<std::string>& note,
        const std::string& agent,
        std::filesystem::path* destination_out = nullptr);
    bool set_ready_fields(
        const std::string& item_id,
        const std::optional<std::string>& context,
        const std::optional<std::string>& goal,
        const std::optional<std::string>& approach,
        const std::optional<std::string>& acceptance_criteria,
        const std::optional<std::string>& risks,
        const std::string& agent);
    
private:
    std::shared_ptr<adapters::FilesystemAdapter> fs_;
    std::filesystem::path product_root_;
    std::filesystem::path items_root_;

    static std::string derive_prefix(const std::string& product);
    static std::string slugify(const std::string& title);
    static std::string generate_uuid_v7_like();
    static std::string iso_date_today();
    static std::string type_code(core::ItemType type);
    static std::string type_dirname(core::ItemType type);
    std::string generate_id(core::ItemType type, const std::string& product) const;
    std::filesystem::path build_item_path(
        core::ItemType type,
        const std::string& item_id,
        const std::string& title) const;
    std::optional<std::filesystem::path> find_item_path(const std::string& item_id) const;
};

} // namespace kano::backlog::ops
