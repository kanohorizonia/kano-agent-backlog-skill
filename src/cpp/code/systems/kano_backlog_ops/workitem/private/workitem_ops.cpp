#include "kano/backlog_ops/workitem/workitem_ops.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_ops/templates/template_ops.hpp"
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

CreateItemResult WorkitemOps::create_item(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& prefix,
    ItemType type,
    const std::string& title,
    const std::string& agent,
    std::optional<std::string> parent,
    std::string priority,
    std::vector<std::string> tags,
    std::string area,
    std::string iteration
) {
    CanonicalStore store(backlog_root);
    
    // 1. Generate ID and UID
    std::string type_code;
    switch (type) {
        case ItemType::Epic: type_code = "EPIC"; break;
        case ItemType::Feature: type_code = "FTR"; break;
        case ItemType::UserStory: type_code = "USR"; break;
        case ItemType::Task: type_code = "TSK"; break;
        case ItemType::Bug: type_code = "BUG"; break;
    }
    
    int number = index.get_next_number(prefix, type_code);
    
    // 2. Prepare item via store
    BacklogItem item = store.create(prefix, type, title, number, parent);
    item.state = ItemState::Proposed;
    item.priority = priority;
    item.area = area;
    item.iteration = iteration;
    item.tags = tags;
    
    // 3. Render content using templates
    std::string content = TemplateOps::render_item_body(item, agent, "Created item");
    
    // 4. Calculate path and write file
    // Note: CanonicalStore should have a way to calculate path without internal knowledge
    // For now we'll use a hack or implement it in store.
    std::filesystem::path item_path = *item.file_path;
    std::filesystem::create_directories(item_path.parent_path());
    
    std::ofstream ofs(item_path);
    ofs << content;
    ofs.close();
    
    // 5. Update index
    item.file_path = item_path;
    index.index_item(item);
    
    return {item.id, item.uid, item_path, type};
}

UpdateStateResult WorkitemOps::update_state(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    ItemState new_state,
    const std::string& agent,
    std::optional<std::string> message,
    bool force
) {
    // 1. Resolve path from index
    auto path_opt = index.get_path_by_id(item_ref);
    if (!path_opt) path_opt = index.get_path_by_uid(item_ref);
    if (!path_opt) throw std::runtime_error("Item not found in index: " + item_ref);
    
    // 2. Read item
    CanonicalStore store(backlog_root);
    BacklogItem item = store.read(*path_opt);
    
    ItemState old_state = item.state;
    if (old_state == new_state) {
        return {item.id, old_state, new_state, false, false, false};
    }

    // 3. Ready Gate Validation
    if (new_state == ItemState::InProgress && !force) {
        auto [ready, gaps] = Validator::is_ready(item);
        if (!ready) {
            std::string gap_msg;
            for (const auto& g : gaps) gap_msg += (gap_msg.empty() ? "" : ", ") + g;
            throw std::runtime_error("Item " + item.id + " is not Ready. Missing fields: " + gap_msg);
        }

        // Check parent
        if (item.parent) {
            auto parent_path = index.get_path_by_id(*item.parent);
            if (!parent_path) parent_path = index.get_path_by_uid(*item.parent);
            if (parent_path) {
                BacklogItem parent_item = store.read(*parent_path);
                auto [p_ready, p_gaps] = Validator::is_ready(parent_item);
                if (!p_ready) {
                    std::string p_gap_msg;
                    for (const auto& g : p_gaps) p_gap_msg += (p_gap_msg.empty() ? "" : ", ") + g;
                    throw std::runtime_error("Parent item " + parent_item.id + " is not Ready. Missing fields: " + p_gap_msg);
                }
            }
        }
    }

    // 4. Auto-assign owner
    if (new_state == ItemState::InProgress) {
        if (!item.owner || item.owner->empty() || *item.owner == "null") {
            item.owner = agent;
        }
    }
    
    // 5. Determine Action for StateMachine
    StateAction action;
    switch (new_state) {
        case ItemState::Proposed: action = StateAction::Propose; break;
        case ItemState::Planned: // Fallthrough
        case ItemState::Ready: action = StateAction::Ready; break;
        case ItemState::InProgress: action = StateAction::Start; break;
        case ItemState::Review: action = StateAction::Review; break;
        case ItemState::Done: action = StateAction::Done; break;
        case ItemState::Blocked: action = StateAction::Block; break;
        case ItemState::Dropped: action = StateAction::Drop; break;
        default: action = StateAction::Propose; break; // Fallback
    }

    std::string final_msg = message.value_or("State update: " + to_string(old_state) + " -> " + to_string(new_state));
    if (new_state == ItemState::InProgress) {
        final_msg += force ? " [Ready gate bypassed via --force]" : " [Ready gate validated]";
    }

    // 6. Transition via StateMachine
    StateMachine::transition(item, action, agent, final_msg);
    
    // 7. Parent sync logic
    bool parent_synced = false;
    if (item.parent) {
        auto parent_path = index.get_path_by_id(*item.parent);
        if (!parent_path) parent_path = index.get_path_by_uid(*item.parent);
        
        if (parent_path) {
            BacklogItem parent_item = store.read(*parent_path);
            ItemState parent_next_state = parent_item.state;

            if (new_state == ItemState::InProgress || new_state == ItemState::Review || new_state == ItemState::Blocked) {
                // If child is active, parent should be InProgress (if it was less than that)
                if (parent_item.state < ItemState::InProgress) {
                    parent_next_state = ItemState::InProgress;
                }
            } else if (new_state == ItemState::Done) {
                // Check siblings
                // For now, this requires a list_items which we'll stub or implement simply
                // In Python: siblings = list_items(parent=item.parent, ...)
                // We'll skip complex sibling check for now to avoid dependency loop or implement a simple index query
            }

            if (parent_next_state != parent_item.state) {
                StateMachine::transition(parent_item, StateAction::Start, agent, 
                    "Auto parent sync: child " + item.id + " -> " + to_string(new_state) + "; parent -> InProgress");
                store.write(parent_item);
                index.index_item(parent_item);
                parent_synced = true;
            }
        }
    }

    // 8. Write back
    store.write(item);
    
    // 9. Update index
    index.index_item(item);
    
    return {item.id, old_state, new_state, true, parent_synced, false};
}

TrashItemResult WorkitemOps::trash_item(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& agent,
    std::optional<std::string> reason
) {
    // 1. Resolve item
    auto path_opt = index.get_path_by_id(item_ref);
    if (!path_opt) path_opt = index.get_path_by_uid(item_ref);
    if (!path_opt) throw std::runtime_error("Item not found: " + item_ref);
    
    std::filesystem::path source_path = *path_opt;
    
    // 2. Calculate trash path
    // Get YYYY-MM-DD
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d");
    std::string stamp = ss.str();
    
    // Calculate relative path from product_root
    std::filesystem::path rel_path = std::filesystem::relative(source_path, backlog_root);
    std::filesystem::path trashed_path = backlog_root / "_trash" / stamp / rel_path;
    
    // 3. Update worklog before moving
    CanonicalStore store(backlog_root);
    BacklogItem item = store.read(source_path);
    StateMachine::record_worklog(item, agent, "Trashed item: " + reason.value_or("duplicate or obsolete"));
    store.write(item);
    
    // 4. Move file
    std::filesystem::create_directories(trashed_path.parent_path());
    std::filesystem::rename(source_path, trashed_path);
    
    // 5. Remove from index (or update path if we still want to track it as trashed)
    // For now we'll remove it to be clean, or we could have a 'Trashed' state.
    // Python implementation moves it out of 'items/' so it's effectively gone from active index.
    index.remove_item(item.id);
    
    return {item_ref, source_path, trashed_path, "trashed", reason};
}

void WorkitemOps::remap_parent(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& new_parent_ref,
    const std::string& agent
) {
    // 1. Resolve item
    auto path_opt = index.get_path_by_id(item_ref);
    if (!path_opt) path_opt = index.get_path_by_uid(item_ref);
    if (!path_opt) throw std::runtime_error("Item not found: " + item_ref);
    
    CanonicalStore store(backlog_root);
    BacklogItem item = store.read(*path_opt);

    // 2. Resolve parent
    if (new_parent_ref == "none" || new_parent_ref.empty() || new_parent_ref == "null") {
        item.parent = std::nullopt;
    } else {
        auto parent_path = index.get_path_by_id(new_parent_ref);
        if (!parent_path) parent_path = index.get_path_by_uid(new_parent_ref);
        if (!parent_path) throw std::runtime_error("Parent item not found: " + new_parent_ref);
        
        BacklogItem parent_item = store.read(*parent_path);
        item.parent = parent_item.id;
    }

    // 3. Mark update and write
    StateMachine::record_worklog(item, agent, "Remapped parent to: " + item.parent.value_or("none"));
    store.write(item);
    
    // 4. Update index
    index.index_item(item);
}

DecisionWritebackResult WorkitemOps::add_decision_writeback(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& decision,
    const std::string& agent,
    std::optional<std::string> source
) {
    // 1. Resolve item
    auto path_opt = index.get_path_by_id(item_ref);
    if (!path_opt) path_opt = index.get_path_by_uid(item_ref);
    if (!path_opt) throw std::runtime_error("Item not found: " + item_ref);
    
    CanonicalStore store(backlog_root);
    BacklogItem item = store.read(*path_opt);
    
    // 2. Add decision
    std::string decision_text = decision;
    if (source) {
        decision_text += " (source: " + *source + ")";
    }
    
    // Check for duplicates
    bool exists = false;
    for (const auto& d : item.decisions) {
        if (d == decision_text) {
            exists = true;
            break;
        }
    }
    
    if (!exists) {
        item.decisions.push_back(decision_text);
    }
    
    // 3. Record worklog
    StateMachine::record_worklog(item, agent, "Decision write-back added: " + decision_text);
    
    // 4. Write back
    store.write(item);
    
    return {item.id, *path_opt, !exists, true};
}

RemapIdResult WorkitemOps::remap_id(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& new_id,
    const std::string& agent
) {
    // 1. Resolve item
    auto path_opt = index.get_path_by_id(item_ref);
    if (!path_opt) path_opt = index.get_path_by_uid(item_ref);
    if (!path_opt) throw std::runtime_error("Item not found: " + item_ref);
    
    std::filesystem::path old_path = *path_opt;
    CanonicalStore store(backlog_root);
    BacklogItem item = store.read(old_path);
    std::string old_id = item.id;
    
    // 2. Calculate new path
    std::string filename = old_path.filename().string();
    size_t underscore_pos = filename.find('_');
    std::string tail = (underscore_pos != std::string::npos) ? filename.substr(underscore_pos) : ".md";
    std::filesystem::path new_path = old_path.parent_path() / (new_id + tail);
    
    // 3. Update ID in item and add worklog
    item.id = new_id;
    StateMachine::record_worklog(item, agent, "Remapped ID: " + old_id + " -> " + new_id);
    
    // 4. Rename file
    if (old_path != new_path) {
        std::filesystem::create_directories(new_path.parent_path());
        std::filesystem::rename(old_path, new_path);
    }
    
    // 5. Write back with new ID
    item.file_path = new_path;
    store.write(item);
    
    // 6. Global search and replace in the product root
    int updated_files = 1;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(backlog_root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            if (entry.path() == new_path) continue;
            
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
            ifs.close();
            
            size_t pos = 0;
            bool changed = false;
            while ((pos = content.find(old_id, pos)) != std::string::npos) {
                bool boundary_before = (pos == 0 || (!std::isalnum(content[pos - 1]) && content[pos - 1] != '-'));
                bool boundary_after = (pos + old_id.length() == content.length() || (!std::isalnum(content[pos + old_id.length()]) && content[pos + old_id.length()] != '-'));
                
                if (boundary_before && boundary_after) {
                    content.replace(pos, old_id.length(), new_id);
                    pos += new_id.length();
                    changed = true;
                } else {
                    pos += old_id.length();
                }
            }
            
            if (changed) {
                std::ofstream ofs(entry.path());
                ofs << content;
                ofs.close();
                updated_files++;
            }
        }
    }
    
    // 7. Update index
    index.remove_item(old_id);
    index.index_item(item);
    
    return {old_id, new_id, old_path, new_path, updated_files};
}

} // namespace kano::backlog_ops
