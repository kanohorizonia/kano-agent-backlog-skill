#pragma once

#include "kano/backlog_core/models/models.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace kano::backlog_ops {

/**
 * WorksetOps manages per-item execution caches.
 * Ported from workset.py
 *
 * Workset directory structure (under _kano/backlog/.cache/worksets/items/<item_id>/):
 *   meta.json     — workset metadata (uid, item_ref, agent, timestamps, ttl)
 *   plan.md       — checklist derived from acceptance criteria
 *   notes.md      — working notes with Decision: markers
 *   deliverables/ — staging area for artifacts before promotion
 */
class WorksetOps {
public:
    // -------------------------------------------------------------------------
    // Result types
    // -------------------------------------------------------------------------

    struct WorksetMetadata {
        std::string workset_id;
        std::string item_id;
        std::string item_uid;
        std::string item_path;
        std::string agent;
        std::string created_at;
        std::string refreshed_at;
        int ttl_hours = 72;
        std::optional<std::string> source_commit;
    };

    struct WorksetInitResult {
        std::filesystem::path workset_path;
        int item_count;
        bool created;       // true = new, false = existing
    };

    struct WorksetNextResult {
        int step_number;       // 1-based, 0 if complete
        std::string description;
        bool is_complete;     // true if all steps done
    };

    struct WorksetRefreshResult {
        std::filesystem::path workset_path;
        int items_added;
        int items_removed;
        int items_updated;
    };

    struct WorksetPromoteResult {
        std::vector<std::string> promoted_files;
        std::filesystem::path target_path;
        std::string worklog_entry;
    };

    struct WorksetCleanupResult {
        int deleted_count;
        std::vector<std::filesystem::path> deleted_paths;
        size_t space_reclaimed_bytes;
    };

    /** A single Decision: marker candidate extracted from notes.md. */
    struct DetectAdrCandidate {
        std::string text;
        std::string suggested_title;
    };

    /** Result of detect_adr — list of ADR candidates found in notes.md. */
    struct DetectAdrResult {
        std::filesystem::path workset_path;
        std::vector<DetectAdrCandidate> candidates;
    };

    /**
     * Scan workset notes.md for Decision: markers.
     * Returns candidates that should be promoted to ADRs.
     */
    static DetectAdrResult detect_adr(
        const std::string& item_ref,
        const std::filesystem::path& backlog_root
    );

    // -------------------------------------------------------------------------
    // Workset operations
    // -------------------------------------------------------------------------

    /**
     * Initialize a workset for an item (by ID, UID, or path).
     * Creates workset directory with meta.json, plan.md, notes.md.
     * Throws WorksetError if item not found.
     */
    static WorksetInitResult init_workset(
        const std::string& item_ref,
        const std::string& agent,
        const std::filesystem::path& backlog_root,
        int ttl_hours = 72
    );

    /**
     * Get the next unchecked action from plan.md.
     * Returns step_number=0, is_complete=true when all done.
     */
    static WorksetNextResult get_next_action(
        const std::string& item_ref,
        const std::filesystem::path& backlog_root
    );

    /**
     * Refresh workset: update refreshed_at timestamp, verify item exists.
     */
    static WorksetRefreshResult refresh_workset(
        const std::string& item_ref,
        const std::string& agent,
        const std::filesystem::path& backlog_root
    );

    /**
     * Promote deliverables/ to canonical artifacts directory.
     * Returns list of promoted files.
     */
    static WorksetPromoteResult promote_deliverables(
        const std::string& item_ref,
        const std::string& agent,
        const std::filesystem::path& backlog_root,
        bool dry_run = false
    );

    /**
     * List all worksets under .cache/worksets/items/.
     */
    static std::vector<WorksetMetadata> list_worksets(
        const std::filesystem::path& backlog_root
    );

    /**
     * Cleanup worksets older than ttl_hours.
     * Returns deleted count and space reclaimed.
     */
    static WorksetCleanupResult cleanup_worksets(
        const std::filesystem::path& backlog_root,
        int ttl_hours = 72,
        bool dry_run = false
    );

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /** Resolve item_ref (ID, UID, or path) to workset directory path. */
    static std::optional<std::filesystem::path> resolve_workset_path(
        const std::string& item_ref,
        const std::filesystem::path& backlog_root
    );

    /** Load meta.json from a workset directory. */
    static std::optional<WorksetMetadata> load_meta(
        const std::filesystem::path& workset_path
    );

    /** Save meta.json to a workset directory. */
    static void save_meta(
        const std::filesystem::path& workset_path,
        const WorksetMetadata& meta
    );
};

} // namespace kano::backlog_ops
