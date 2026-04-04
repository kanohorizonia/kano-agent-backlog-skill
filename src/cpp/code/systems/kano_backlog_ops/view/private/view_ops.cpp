#include "kano/backlog_ops/view/view_ops.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <fstream>

namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

std::string group_for_state(ItemState state) {
    switch (state) {
        case ItemState::New:
        case ItemState::Proposed:
        case ItemState::Planned:
        case ItemState::Ready:
            return "New";
        case ItemState::InProgress:
        case ItemState::Review:
        case ItemState::Blocked:
            return "InProgress";
        case ItemState::Done:
        case ItemState::Dropped:
            return "Done";
    }
    return "New";
}

std::string title_for_group(const std::string& group) {
    if (group == "New") return "New Work";
    if (group == "Done") return "Done Work";
    return "InProgress Work";
}

std::string filename_for_group(const std::string& group) {
    if (group == "New") return "Dashboard_PlainMarkdown_New.md";
    if (group == "Done") return "Dashboard_PlainMarkdown_Done.md";
    return "Dashboard_PlainMarkdown_Active.md";
}

int type_rank(ItemType type) {
    switch (type) {
        case ItemType::Epic: return 0;
        case ItemType::Feature: return 1;
        case ItemType::UserStory: return 2;
        case ItemType::Task: return 3;
        case ItemType::Bug: return 4;
    }
    return 99;
}

std::string relative_link(const std::filesystem::path& target, const std::filesystem::path& base) {
    return std::filesystem::relative(target, base).generic_string();
}

std::string render_dashboard(
    const std::string& title,
    const std::string& group,
    const std::map<ItemType, std::vector<BacklogItem>>& grouped_items,
    const std::filesystem::path& output_path,
    const std::string& agent
) {
    std::stringstream ss;
    ss << "# " << title << "\n\n";
    ss << "Source: items\n";
    ss << "Agent: " << agent << "\n\n";
    ss << "## " << group << "\n\n";

    bool wrote_any_type = false;
    for (const auto& [type, items] : grouped_items) {
        if (items.empty()) {
            continue;
        }
        wrote_any_type = true;
        ss << "### " << to_string(type) << "\n\n";
        for (const auto& item : items) {
            if (!item.file_path) {
                continue;
            }
            std::string description = item.id + " " + item.title;
            std::vector<std::string> indicators;
            if (!item.links.blocked_by.empty()) {
                std::stringstream blocked;
                blocked << "🔴 Blocked by: ";
                for (size_t i = 0; i < item.links.blocked_by.size(); ++i) {
                    if (i > 0) blocked << ", ";
                    blocked << item.links.blocked_by[i];
                }
                indicators.push_back(blocked.str());
            }
            if (!item.links.blocks.empty()) {
                std::stringstream blocks;
                blocks << "⛓️ Blocks: ";
                for (size_t i = 0; i < item.links.blocks.size(); ++i) {
                    if (i > 0) blocks << ", ";
                    blocks << item.links.blocks[i];
                }
                indicators.push_back(blocks.str());
            }
            if (!indicators.empty()) {
                ss << "- [" << description << " [";
                for (size_t i = 0; i < indicators.size(); ++i) {
                    if (i > 0) ss << " | ";
                    ss << indicators[i];
                }
                ss << "]]";
            } else {
                ss << "- [" << description << "]";
            }
            ss << "(" << relative_link(*item.file_path, output_path.parent_path()) << ")\n";
        }
        ss << "\n";
    }

    if (!wrote_any_type) {
        ss << "_No items._\n\n";
    }

    return ss.str();
}

} // namespace

std::vector<IndexItem> ViewOps::list_items(BacklogIndex& index, const ViewFilter& filter) {
    // For now, we delegate simple type/state filtering to the index's query method.
    // In a more advanced implementation, we'd add complex filtering here.
    return index.query_items(filter.type, filter.state);
}

std::string ViewOps::render_table(const std::vector<IndexItem>& items) {
    if (items.empty()) {
        return "No items found.\n";
    }

    std::stringstream ss;
    
    // Header
    ss << std::left 
       << std::setw(20) << "ID" 
       << std::setw(15) << "Type" 
       << std::setw(15) << "State" 
       << "Title" << "\n";
    ss << std::string(80, '-') << "\n";

    for (const auto& item : items) {
        ss << std::left 
           << std::setw(20) << item.id 
           << std::setw(15) << to_string(item.type) 
           << std::setw(15) << to_string(item.state) 
           << item.title << "\n";
    }

    return ss.str();
}

RefreshDashboardsResult ViewOps::refresh_dashboards(
    const std::filesystem::path& product_root,
    const std::string& agent
) {
    CanonicalStore store(product_root);
    std::vector<BacklogItem> items;
    for (const auto& path : store.list_items()) {
        const auto filename = path.filename().string();
        if (has_suffix(filename, ".index.md") || path.filename() == "README.md") {
            continue;
        }
        try {
            items.push_back(store.read(path));
        } catch (const std::exception&) {
            continue;
        }
    }

    std::sort(items.begin(), items.end(), [](const BacklogItem& left, const BacklogItem& right) {
        if (type_rank(left.type) != type_rank(right.type)) {
            return type_rank(left.type) < type_rank(right.type);
        }
        return left.id < right.id;
    });

    std::filesystem::path views_root = product_root / "views";
    std::filesystem::create_directories(views_root);

    RefreshDashboardsResult result;
    for (const std::string& group : {std::string("InProgress"), std::string("New"), std::string("Done")}) {
        std::map<ItemType, std::vector<BacklogItem>> grouped;
        for (const auto& item : items) {
            if (group_for_state(item.state) == group) {
                grouped[item.type].push_back(item);
            }
        }

        std::filesystem::path output_path = views_root / filename_for_group(group);
        std::ofstream out(output_path);
        if (!out.is_open()) {
            throw WriteError("Failed to open " + output_path.string() + " for writing");
        }
        out << render_dashboard(title_for_group(group), group, grouped, output_path, agent);
        result.views_refreshed.push_back(output_path);
    }

    return result;
}

} // namespace kano::backlog_ops
