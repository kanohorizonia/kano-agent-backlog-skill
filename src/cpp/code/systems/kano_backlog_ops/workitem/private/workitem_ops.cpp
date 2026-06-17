#include "kano/backlog_ops/workitem/workitem_ops.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_ops/view/view_ops.hpp"
#include "kano/backlog_ops/templates/template_ops.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <cctype>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

BacklogItem resolve_item_or_throw(const CanonicalStore& store, const std::string& item_ref) {
    RefResolver resolver(store);
    return resolver.resolve(item_ref);
}

std::filesystem::path normalize_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized.lexically_normal();
    }

    auto absolute = std::filesystem::absolute(path, ec);
    if (!ec) {
        return absolute.lexically_normal();
    }

    return path.lexically_normal();
}

bool is_inside_root(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto normalized_path = normalize_path(path);
    const auto normalized_root = normalize_path(root);
    std::error_code ec;
    auto relative = std::filesystem::relative(normalized_path, normalized_root, ec);
    if (ec || relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool looks_like_path_ref(const std::string& ref) {
    return ref.find('/') != std::string::npos ||
           ref.find('\\') != std::string::npos ||
           ref.find(':') != std::string::npos;
}

std::string parent_ref_for_diagnostic(const std::string& ref) {
    if (looks_like_path_ref(ref)) {
        return "<path-like parent ref redacted>";
    }
    return ref;
}

std::string path_for_diagnostic(
    const std::filesystem::path& path,
    const std::filesystem::path& product_root
) {
    const auto normalized_path = normalize_path(path);
    const auto normalized_root = normalize_path(product_root);
    std::error_code ec;
    auto relative = std::filesystem::relative(normalized_path, normalized_root, ec);
    if (!ec && !relative.empty() && !relative.is_absolute()) {
        bool escapes_root = false;
        for (const auto& component : relative) {
            if (component == "..") {
                escapes_root = true;
                break;
            }
        }
        if (!escapes_root) {
            return "active-root/" + relative.generic_string();
        }
    }

    const auto filename = normalized_path.filename().generic_string();
    if (!filename.empty()) {
        return "outside-active-root/" + filename;
    }
    return "outside-active-root/<redacted>";
}

struct ParentIndexedRead {
    std::optional<BacklogItem> item;
    bool stale_indexed_path = false;
    std::string stale_reason;
};

ParentIndexedRead read_indexed_item_if_active(
    const CanonicalStore& store,
    const std::filesystem::path& product_root,
    const std::optional<std::filesystem::path>& indexed_path,
    const std::string& parent_ref
) {
    ParentIndexedRead outcome;
    if (!indexed_path) {
        return outcome;
    }
    if (!is_inside_root(*indexed_path, product_root)) {
        outcome.stale_indexed_path = true;
        outcome.stale_reason =
            "indexed path is outside active product root: " +
            path_for_diagnostic(*indexed_path, product_root);
        return outcome;
    }
    if (!std::filesystem::exists(*indexed_path)) {
        outcome.stale_indexed_path = true;
        outcome.stale_reason =
            "indexed path no longer exists on disk: " +
            path_for_diagnostic(*indexed_path, product_root);
        return outcome;
    }
    auto item = store.read(*indexed_path);
    if (item.id != parent_ref && item.uid != parent_ref) {
        outcome.stale_indexed_path = true;
        outcome.stale_reason =
            "indexed file no longer matches parent ref id/uid: " +
            path_for_diagnostic(*indexed_path, product_root);
        return outcome;
    }
    outcome.item = item;
    return outcome;
}

BacklogItem resolve_parent_by_identity(
    const CanonicalStore& store,
    const std::filesystem::path& product_root,
    const std::string& parent_ref,
    bool saw_stale_indexed_path,
    const std::string& stale_indexed_path_reason
) {
    std::vector<BacklogItem> matches;
    std::vector<std::string> read_failures;
    for (const auto& path : store.list_items()) {
        try {
            auto item = store.read(path);
            if (item.id == parent_ref || item.uid == parent_ref) {
                matches.push_back(item);
            }
        } catch (const std::exception&) {
            read_failures.push_back(path_for_diagnostic(path, product_root) + ": read failure");
        } catch (...) {
            read_failures.push_back(path_for_diagnostic(path, product_root) + ": unknown read failure");
        }
    }

    if (matches.size() == 1) {
        return matches.front();
    }

    if (matches.size() > 1) {
        throw std::runtime_error(
            "Ambiguous parent item reference: " + parent_ref_for_diagnostic(parent_ref));
    }

    std::string message =
        "Parent item not found in active product root: " + parent_ref_for_diagnostic(parent_ref);
    if (saw_stale_indexed_path) {
        message += " (stale index/path cache: " + stale_indexed_path_reason + ")";
    }
    if (!read_failures.empty()) {
        const std::size_t max_failures = 3;
        message += " (read errors while scanning active items: ";
        const std::size_t shown = std::min(read_failures.size(), max_failures);
        for (std::size_t i = 0; i < shown; ++i) {
            if (i > 0) message += "; ";
            message += read_failures[i];
        }
        if (read_failures.size() > max_failures) {
            message += "; ... and " + std::to_string(read_failures.size() - max_failures) + " more";
        }
        message += ")";
    }
    throw std::runtime_error(message);
}

BacklogItem resolve_parent_item(
    BacklogIndex& index,
    const CanonicalStore& store,
    const std::filesystem::path& product_root,
    const std::string& parent_ref
) {
    bool saw_stale_indexed_path = false;
    std::string stale_indexed_path_reason;

    auto id_path = index.get_path_by_id(parent_ref);
    auto id_attempt = read_indexed_item_if_active(store, product_root, id_path, parent_ref);
    if (id_attempt.item) {
        return *id_attempt.item;
    }
    if (id_attempt.stale_indexed_path) {
        saw_stale_indexed_path = true;
        stale_indexed_path_reason = id_attempt.stale_reason;
    }

    auto uid_path = index.get_path_by_uid(parent_ref);
    auto uid_attempt = read_indexed_item_if_active(store, product_root, uid_path, parent_ref);
    if (uid_attempt.item) {
        return *uid_attempt.item;
    }
    if (uid_attempt.stale_indexed_path && !saw_stale_indexed_path) {
        saw_stale_indexed_path = true;
        stale_indexed_path_reason = uid_attempt.stale_reason;
    }

    return resolve_parent_by_identity(
        store,
        product_root,
        parent_ref,
        saw_stale_indexed_path,
        stale_indexed_path_reason
    );
}

} // namespace

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
    CanonicalStore store(backlog_root);
    BacklogItem item = resolve_item_or_throw(store, item_ref);
    if (!item.file_path) {
        throw std::runtime_error("Resolved item has no file path: " + item_ref);
    }
    
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
            BacklogItem parent_item = resolve_parent_item(index, store, backlog_root, *item.parent);
            auto [p_ready, p_gaps] = Validator::is_ready(parent_item);
            if (!p_ready) {
                std::string p_gap_msg;
                for (const auto& g : p_gaps) p_gap_msg += (p_gap_msg.empty() ? "" : ", ") + g;
                throw std::runtime_error("Parent item " + parent_item.id + " is not Ready. Missing fields: " + p_gap_msg);
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
        BacklogItem parent_item = resolve_parent_item(index, store, backlog_root, *item.parent);
        ItemState parent_next_state = parent_item.state;

        if (new_state == ItemState::InProgress || new_state == ItemState::Review || new_state == ItemState::Blocked) {
            // If child is active, parent should be InProgress (if it was less than that)
            if (parent_item.state < ItemState::InProgress) {
                parent_next_state = ItemState::InProgress;
            }
        } else if (new_state == ItemState::Done) {
            // Parent synchronization is forward-only. Child completion does not
            // automatically complete the parent because sibling readiness and
            // acceptance ownership require an explicit parent transition.
        }

        if (parent_next_state != parent_item.state) {
            StateMachine::transition(parent_item, StateAction::Start, agent,
                "Auto parent sync: child " + item.id + " -> " + to_string(new_state) + "; parent -> InProgress");
            store.write(parent_item);
            index.index_item(parent_item);
            parent_synced = true;
        }
    }

    // 8. Write back
    store.write(item);
    auto refreshed = ViewOps::refresh_dashboards(backlog_root, agent);
    
    // 9. Update index
    index.index_item(item);
    
    return {item.id, old_state, new_state, true, parent_synced, !refreshed.views_refreshed.empty()};
}

TrashItemResult WorkitemOps::trash_item(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& agent,
    std::optional<std::string> reason
) {
    CanonicalStore store(backlog_root);
    BacklogItem item = resolve_item_or_throw(store, item_ref);
    if (!item.file_path) {
        throw std::runtime_error("Resolved item has no file path: " + item_ref);
    }

    std::filesystem::path source_path = *item.file_path;

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
    StateMachine::record_worklog(item, agent, "Trashed item: " + reason.value_or("duplicate or obsolete"));
    store.write(item);
    
    // 4. Move file
    std::filesystem::create_directories(trashed_path.parent_path());
    std::filesystem::rename(source_path, trashed_path);
    
    // 5. Remove from index (or update path if we still want to track it as trashed)
    // For now we'll remove it to be clean, or we could have a 'Trashed' state.
    // Moving it out of 'items/' removes it from the active item index.
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
    CanonicalStore store(backlog_root);
    RefResolver resolver(store);
    BacklogItem item = resolver.resolve(item_ref);

    // 2. Resolve parent
    if (new_parent_ref == "none" || new_parent_ref.empty() || new_parent_ref == "null") {
        item.parent = std::nullopt;
    } else {
        BacklogItem parent_item = resolver.resolve(new_parent_ref);
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
    CanonicalStore store(backlog_root);
    BacklogItem item = resolve_item_or_throw(store, item_ref);
    if (!item.file_path) {
        throw std::runtime_error("Resolved item has no file path: " + item_ref);
    }
    
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
    
    return {item.id, *item.file_path, !exists, true};
}

RemapIdResult WorkitemOps::remap_id(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    const std::string& new_id,
    const std::string& agent
) {
    CanonicalStore store(backlog_root);
    BacklogItem item = resolve_item_or_throw(store, item_ref);
    if (!item.file_path) {
        throw std::runtime_error("Resolved item has no file path: " + item_ref);
    }

    std::filesystem::path old_path = *item.file_path;
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
