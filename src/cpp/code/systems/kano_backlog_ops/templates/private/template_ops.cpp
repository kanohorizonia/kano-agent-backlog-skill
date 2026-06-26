#include "kano/backlog_ops/templates/template_ops.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

std::string TemplateOps::render_item_body(
    const BacklogItem& item,
    const std::string& agent,
    const std::string& worklog_message,
    const std::optional<std::string>& model
) {
    std::stringstream ss;
    ss << render_frontmatter(item) << "\n";
    
    // Get timestamp
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    
    ss << "\n# Context\n\n";
    ss << "# Goal\n\n";
    ss << "# Non-Goals / Do Not\n\n";
    ss << "# Intent Amendments\n\n";
    ss << "# Approach\n\n";
    ss << "# Alternatives\n\n";
    ss << "# Acceptance Criteria\n\n";
    ss << "# Risks / Dependencies\n\n";
    ss << "# Worklog\n\n";
    
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M") << " [agent=" << agent << "]";
    if (model && !model->empty()) {
        ss << " [model=" << *model << "]";
    }
    ss << " " << worklog_message << "\n";

    return ss.str();
}

std::string TemplateOps::render_epic_index(
    const std::string& item_id,
    const std::string& title,
    const std::string& updated,
    const std::string& backlog_root_label
) {
    std::stringstream ss;
    ss << "---\n";
    ss << "type: Index\n";
    ss << "for: " << item_id << "\n";
    ss << "title: \"" << title << " Index\"\n";
    ss << "updated: " << updated << "\n";
    ss << "---\n\n";
    
    ss << "# MOC\n\n";
    ss << "## Auto list (Dataview)\n\n";
    ss << "```dataview\n";
    ss << "table id, state, priority\n";
    ss << "from \"" << backlog_root_label << "/items\"\n";
    ss << "where parent = \"" << item_id << "\"\n";
    ss << "sort priority asc\n";
    ss << "```\n\n";
    
    ss << "## Manual list\n\n";
    ss << "<!-- Add children manually here if needed -->\n\n";

    return ss.str();
}

std::string TemplateOps::render_frontmatter(const BacklogItem& item) {
    std::stringstream ss;
    ss << "---\n";
    ss << "id: " << item.id << "\n";
    ss << "uid: " << item.uid << "\n";
    ss << "type: " << to_string(item.type) << "\n";
    ss << "title: \"" << item.title << "\"\n";
    ss << "state: " << to_string(item.state) << "\n";
    ss << "priority: " << (item.priority ? *item.priority : "P2") << "\n";
    ss << "parent: " << (item.parent ? *item.parent : "null") << "\n";
    ss << "area: " << (item.area ? *item.area : "general") << "\n";
    ss << "iteration: " << (item.iteration ? *item.iteration : "backlog") << "\n";
    ss << "work_intent: " << (item.work_intent ? *item.work_intent : "implementation") << "\n";
    ss << "execution_mode: " << (item.execution_mode ? *item.execution_mode : "null") << "\n";
    ss << "result_contract: " << (item.result_contract ? *item.result_contract : "null") << "\n";
    ss << "evidence_requirement: " << (item.evidence_requirement ? *item.evidence_requirement : "null") << "\n";
    ss << "follow_up_policy: " << (item.follow_up_policy ? *item.follow_up_policy : "null") << "\n";
    ss << "no_go_or_defer_policy: " << (item.no_go_or_defer_policy ? *item.no_go_or_defer_policy : "null") << "\n";
    ss << "intent.author: " << (item.intent_author ? *item.intent_author : "null") << "\n";
    ss << "intent.source: " << (item.intent_source ? *item.intent_source : "null") << "\n";
    ss << "intent.owner: " << (item.intent_owner ? *item.intent_owner : "null") << "\n";
    ss << "intent.reviewers: []\n";
    ss << "intent.conflicts_with: []\n";
    ss << "intent.supersedes: []\n";

    ss << "tags: [";
    for (size_t i = 0; i < item.tags.size(); ++i) {
        ss << "\"" << item.tags[i] << "\"";
        if (i < item.tags.size() - 1) ss << ", ";
    }
    ss << "]\n";
    
    ss << "created: " << item.created << "\n";
    ss << "updated: " << item.updated << "\n";
    ss << "owner: " << (item.owner ? *item.owner : "null") << "\n";
    
    ss << "external:\n";
    auto emit_external = [&](const std::string& key, const std::string& fallback) {
        auto it = item.external.find(key);
        ss << "  " << key << ": " << (it != item.external.end() ? it->second : fallback) << "\n";
    };
    emit_external("azure_id", "null");
    emit_external("jira_key", "null");
    for (const auto& [key, value] : item.external) {
        if (key == "azure_id" || key == "jira_key") {
            continue;
        }
        ss << "  " << key << ": " << value << "\n";
    }
    
    ss << "links:\n";
    ss << "  relates: []\n";
    ss << "  blocks: []\n";
    ss << "  blocked_by: []\n";
    
    ss << "decisions: []\n";
    ss << "---";

    return ss.str();
}

} // namespace kano::backlog_ops
