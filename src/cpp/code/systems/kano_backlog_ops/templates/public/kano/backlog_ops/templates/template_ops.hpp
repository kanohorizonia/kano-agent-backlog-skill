#pragma once

#include "kano/backlog_core/models/models.hpp"
#include <string>
#include <vector>
#include <optional>

namespace kano::backlog_ops {

class TemplateOps {
public:
    /**
     * Render the complete markdown content for a new item.
     * Render the native canonical item body.
     */
    static std::string render_item_body(
        const kano::backlog_core::BacklogItem& item,
        const std::string& agent,
        const std::string& worklog_message,
        const std::optional<std::string>& model = std::nullopt
    );

    /**
     * Render an Epic index MOC (Map of Content).
     * Render the native canonical epic index.
     */
    static std::string render_epic_index(
        const std::string& item_id,
        const std::string& title,
        const std::string& updated,
        const std::string& backlog_root_label = "../../.."
    );

private:
    static std::string render_frontmatter(const kano::backlog_core::BacklogItem& item);
};

} // namespace kano::backlog_ops
