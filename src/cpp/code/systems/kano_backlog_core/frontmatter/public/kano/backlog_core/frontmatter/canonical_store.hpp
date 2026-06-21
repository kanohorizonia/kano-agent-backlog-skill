#pragma once

#include "kano/backlog_core/models/models.hpp"
#include <filesystem>
#include <vector>
#include <optional>
#include <string>

namespace kano::backlog_core {

class CanonicalStore {
public:
    explicit CanonicalStore(const std::filesystem::path& product_root);

    /**
     * Read and parse a markdown backlog item from file.
     * Throws ItemNotFoundError or ParseError.
     */
    BacklogItem read(const std::filesystem::path& item_path) const;

    /**
     * Read only frontmatter-backed metadata from a markdown backlog item.
     * Body sections are intentionally skipped for scans such as dashboard refresh.
     */
    BacklogItem read_metadata(const std::filesystem::path& item_path) const;

    /**
     * Write item to file, preserving frontmatter and body structure.
     * Throws ValidationError or WriteError.
     */
    void write(BacklogItem& item) const;

    /**
     * Create a new item with auto-generated ID, UID, and file path.
     * Does NOT write to disk yet.
     */
    BacklogItem create(
        const std::string& prefix,
        ItemType type, 
        const std::string& title, 
        int next_number,
        const std::optional<std::string>& parent = std::nullopt
    ) const;

    /**
     * List all item file paths, optionally filtered by type.
     */
    std::vector<std::filesystem::path> list_items(std::optional<ItemType> type = std::nullopt) const;

    /**
     * Resolve the deterministic path bucket for a display id when possible.
     * Returns nullopt for unsupported ids, missing files, or ambiguous matches.
     */
    std::optional<std::filesystem::path> find_item_path_by_id(const std::string& id) const;

    // Helpers
    int get_next_id_number(ItemType type) const;
    static std::string slugify(const std::string& text);
    static std::string generate_uuid_v7();

private:
    std::filesystem::path product_root_;
    std::filesystem::path items_root_;
};

} // namespace kano::backlog_core
