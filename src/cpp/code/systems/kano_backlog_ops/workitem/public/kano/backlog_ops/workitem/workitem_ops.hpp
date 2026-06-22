#pragma once

#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace kano::backlog_ops {

struct IntentStackEntry {
    kano::backlog_core::BacklogItem item;
    std::string role;
    int depth = 0;
};

struct IntentStackResult {
    std::vector<IntentStackEntry> chain;
    std::vector<std::string> warnings;
    std::vector<std::string> evidence_refs;
};

class WorkitemOps {
public:
    /**
     * Create a new backlog item.
     */
    static kano::backlog_core::CreateItemResult create_item(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& prefix,
        kano::backlog_core::ItemType type,
        const std::string& title,
        const std::string& agent,
        std::optional<std::string> parent = std::nullopt,
        std::string priority = "P2",
        std::vector<std::string> tags = {},
        std::string area = "general",
        std::string iteration = "backlog",
        std::optional<std::string> owner = std::nullopt,
        std::optional<std::string> reviewer = std::nullopt,
        std::string owner_source = "",
        std::string reviewer_source = ""
    );

    /**
     * Update item state with transition validation.
     */
    static kano::backlog_core::UpdateStateResult update_state(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        kano::backlog_core::ItemState new_state,
        const std::string& agent,
        std::optional<std::string> message = std::nullopt,
        bool force = false,
        bool refresh_views = false
    );

    /**
     * Change parent of an item.
     */
    static void remap_parent(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        const std::string& new_parent_ref,
        const std::string& agent
    );

    /**
     * Trash an item by moving it to the _trash repository.
     */
    static kano::backlog_core::TrashItemResult trash_item(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        const std::string& agent,
        std::optional<std::string> reason = std::nullopt
    );

    /**
     * Record a decision in a work item's frontmatter and body.
     */
    static kano::backlog_core::DecisionWritebackResult add_decision_writeback(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        const std::string& decision,
        const std::string& agent,
        std::optional<std::string> source = std::nullopt
    );

    /**
     * Remap an item's ID (rename file and update references).
     */
    static kano::backlog_core::RemapIdResult remap_id(
        BacklogIndex& index,
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        const std::string& new_id,
        const std::string& agent
    );

    /**
     * Resolve a read-only current-item-to-parent intent stack.
     */
    static IntentStackResult resolve_intent_stack(
        const std::filesystem::path& backlog_root,
        const std::string& item_ref,
        int max_depth = 8
    );
};

} // namespace kano::backlog_ops
