#include "kano/backlog_ops/workitem/workitem_ops.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
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
#include <set>

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

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim_text(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool text_contains_any(const std::optional<std::string>& text, const std::vector<std::string>& needles) {
    if (!text) {
        return false;
    }
    const std::string haystack = lower_copy(*text);
    return std::any_of(needles.begin(), needles.end(), [&](const std::string& needle) {
        return haystack.find(needle) != std::string::npos;
    });
}

bool worklog_contains_any(const BacklogItem& item, const std::vector<std::string>& needles) {
    return std::any_of(item.worklog.begin(), item.worklog.end(), [&](const std::string& line) {
        const std::string haystack = lower_copy(line);
        return std::any_of(needles.begin(), needles.end(), [&](const std::string& needle) {
            return haystack.find(needle) != std::string::npos;
        });
    });
}

std::vector<std::string> split_evidence_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> branch_convergence_evidence_lines(const BacklogItem& item) {
    std::vector<std::string> lines;
    if (item.intent_amendments) {
        auto amendment_lines = split_evidence_lines(*item.intent_amendments);
        lines.insert(lines.end(), amendment_lines.begin(), amendment_lines.end());
    }
    lines.insert(lines.end(), item.worklog.begin(), item.worklog.end());
    return lines;
}

bool evidence_contains_any(const std::vector<std::string>& lines, const std::vector<std::string>& needles) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        const std::string haystack = lower_copy(line);
        return std::any_of(needles.begin(), needles.end(), [&](const std::string& needle) {
            return haystack.find(needle) != std::string::npos;
        });
    });
}

std::optional<std::string> extract_evidence_token(
    const std::vector<std::string>& lines,
    const std::string& key,
    bool require_branch_convergence_line = false
) {
    const std::string key_pattern = lower_copy(key) + "=";
    for (const auto& line : lines) {
        const std::string lowered = lower_copy(line);
        if (require_branch_convergence_line && lowered.find("branch convergence:") == std::string::npos) {
            continue;
        }
        const auto key_pos = lowered.find(key_pattern);
        if (key_pos == std::string::npos) {
            continue;
        }
        auto value_begin = key_pos + key_pattern.size();
        while (value_begin < line.size() && std::isspace(static_cast<unsigned char>(line[value_begin]))) {
            ++value_begin;
        }
        auto value_end = value_begin;
        while (value_end < line.size()) {
            const auto ch = static_cast<unsigned char>(line[value_end]);
            if (std::isspace(ch) || line[value_end] == ';' || line[value_end] == ',') {
                break;
            }
            ++value_end;
        }
        auto value = trim_text(line.substr(value_begin, value_end - value_begin));
        if (!value.empty()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> extract_evidence_field(const std::string& line, const std::string& key) {
    const std::string key_pattern = lower_copy(key) + "=";
    const std::string lowered = lower_copy(line);
    const auto key_pos = lowered.find(key_pattern);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto value_begin = key_pos + key_pattern.size();
    while (value_begin < line.size() && std::isspace(static_cast<unsigned char>(line[value_begin]))) {
        ++value_begin;
    }
    auto value_end = line.find(';', value_begin);
    if (value_end == std::string::npos) {
        value_end = line.size();
    }
    auto value = trim_text(line.substr(value_begin, value_end - value_begin));
    if (!value.empty()) {
        return value;
    }
    return std::nullopt;
}

bool evidence_yes(const std::optional<std::string>& value) {
    if (!value) {
        return false;
    }
    const auto normalized = lower_copy(trim_text(*value));
    return normalized == "true" || normalized == "yes";
}

struct BlockedConvergenceEvidence {
    bool present = false;
    std::optional<std::string> branch;
    std::optional<std::string> reason;
    std::optional<std::string> next;
    std::optional<std::string> blocker;
};

BlockedConvergenceEvidence find_blocked_convergence(const std::vector<std::string>& lines) {
    BlockedConvergenceEvidence evidence;
    for (const auto& line : lines) {
        if (lower_copy(line).find("blocked convergence:") == std::string::npos) {
            continue;
        }
        evidence.present = true;
        evidence.branch = extract_evidence_field(line, "branch");
        evidence.reason = extract_evidence_field(line, "reason");
        evidence.next = extract_evidence_field(line, "next");
        evidence.blocker = extract_evidence_field(line, "blocker");
        return evidence;
    }
    return evidence;
}

std::string value_or_missing(const std::optional<std::string>& value) {
    return value && !trim_text(*value).empty() ? trim_text(*value) : "<missing>";
}

std::vector<std::string> branch_convergence_diagnostics(const BacklogItem& item) {
    std::vector<std::string> diagnostics;
    const auto lines = branch_convergence_evidence_lines(item);
    const auto blocked = find_blocked_convergence(lines);
    if (blocked.present) {
        diagnostics.push_back(
            "Review->Done branch convergence: blocked convergence recorded; item should remain not Done until resolved "
            "(branch=" + value_or_missing(blocked.branch) +
            "; blocker=" + value_or_missing(blocked.blocker) +
            "; reason=" + value_or_missing(blocked.reason) +
            "; next=" + value_or_missing(blocked.next) + ").");
        return diagnostics;
    }

    const bool has_target = extract_evidence_token(lines, "target_branch").has_value() ||
        extract_evidence_token(lines, "target", true).has_value();
    if (!has_target) {
        diagnostics.push_back(
            "Review->Done branch convergence: missing target branch evidence; record "
            "Branch convergence: target=<repo-default-branch> unless a human explicitly names another target.");
    }

    if (!extract_evidence_token(lines, "implementation_commit").has_value()) {
        diagnostics.push_back(
            "Review->Done branch convergence: missing implementation_commit=<sha> evidence.");
    }

    if (!evidence_yes(extract_evidence_token(lines, "reachable_from_target"))) {
        diagnostics.push_back(
            "Review->Done branch convergence: missing reachable_from_target=true/yes evidence for the implementation commit.");
    }

    if (!extract_evidence_token(lines, "remote_publication").has_value()) {
        diagnostics.push_back(
            "Review->Done branch convergence: missing remote_publication=<remote/ref> or true/yes evidence.");
    }

    const auto side_branch_delivery = extract_evidence_token(lines, "side_branch_delivery");
    const bool mentions_side_branch = side_branch_delivery.has_value() ||
        evidence_contains_any(lines, {"side-branch", "side branch"});
    if (mentions_side_branch) {
        const auto normalized = side_branch_delivery ? lower_copy(trim_text(*side_branch_delivery)) : "";
        if (normalized != "explicit-human-choice" && normalized != "human-approved") {
            diagnostics.push_back(
                "Review->Done branch convergence: side-branch delivery lacks explicit human choice; "
                "record side_branch_delivery=explicit-human-choice or human-approved.");
        }
    }

    const bool has_nested_gitlink = extract_evidence_token(lines, "nested_gitlink").has_value();
    const bool mentions_nested_work = evidence_contains_any(lines, {
        "nested/submodule", "submodule", "nested repo", "nested-repo", "nested work", "gitlink"
    });
    if (mentions_nested_work && !has_nested_gitlink) {
        diagnostics.push_back(
            "Review->Done branch convergence: nested/submodule work marker found without nested_gitlink evidence; "
            "record parent gitlink/submodule pointer evidence.");
    }

    return diagnostics;
}

bool has_parent_ref(const BacklogItem& item) {
    if (!item.parent) {
        return false;
    }
    const auto parent = trim_text(*item.parent);
    return !parent.empty() && parent != "~" && parent != "null";
}

bool is_high_risk_item(const BacklogItem& item) {
    if (item.type == ItemType::Bug || item.type == ItemType::Issue) {
        return true;
    }
    if (item.priority && (*item.priority == "P0" || *item.priority == "P1")) {
        return true;
    }
    return text_contains_any(item.risks, {"high risk", "high-risk", "security", "data loss", "migration"});
}

std::vector<std::string> intent_transition_diagnostics(
    const std::filesystem::path& backlog_root,
    const BacklogItem& item,
    ItemState old_state,
    ItemState new_state
) {
    std::vector<std::string> diagnostics;

    if (old_state == ItemState::Proposed && new_state == ItemState::Ready) {
        if (!has_parent_ref(item)) {
            diagnostics.push_back("Proposed->Ready intent readiness: missing parent intent; confirm this item is intentionally root-scoped.");
        }
        if (is_high_risk_item(item) && (!item.non_goals || trim_text(*item.non_goals).empty())) {
            diagnostics.push_back("Proposed->Ready intent readiness: high-risk item has no Non-Goals / Do Not constraints recorded.");
        }
    }

    if (old_state == ItemState::Ready && new_state == ItemState::InProgress) {
        const auto stack = WorkitemOps::resolve_intent_stack(backlog_root, item.id, 8);
        diagnostics.push_back("Ready->InProgress intent preflight: run or review `workitem intent-template " + item.id + " --kind preflight` before implementation.");
        for (const auto& warning : stack.warnings) {
            diagnostics.push_back("Ready->InProgress intent-stack: " + warning);
        }
        if (stack.chain.size() <= 1) {
            diagnostics.push_back("Ready->InProgress intent-stack: no parent intent resolved for this item.");
        }
    }

    if (old_state == ItemState::InProgress && new_state == ItemState::Review) {
        const bool has_compliance = text_contains_any(item.intent_amendments, {"do not compliance report", "ok/warn/violation", "compliance"}) ||
            worklog_contains_any(item, {"do not compliance report", "ok/warn/violation", "compliance"});
        if (!has_compliance) {
            diagnostics.push_back("InProgress->Review intent compliance: no Do Not Compliance Report evidence detected; include compliance findings in review evidence.");
        }
    }

    if (old_state == ItemState::Review && new_state == ItemState::Done) {
        const bool has_unresolved_drift = text_contains_any(item.intent_amendments, {"drift finding", "violation", "blocks done", "unresolved"}) ||
            worklog_contains_any(item, {"drift finding", "violation", "blocks done", "unresolved"});
        if (has_unresolved_drift) {
            diagnostics.push_back("Review->Done intent alignment: unresolved drift or Do Not violation evidence detected; confirm explicit resolution before accepting Done.");
        }
        auto convergence_diagnostics = branch_convergence_diagnostics(item);
        diagnostics.insert(diagnostics.end(), convergence_diagnostics.begin(), convergence_diagnostics.end());
    }

    return diagnostics;
}

std::string intent_stack_role_for_type(ItemType type) {
    switch (type) {
        case ItemType::Epic: return "epic";
        case ItemType::Feature: return "feature";
        case ItemType::UserStory: return "story";
        case ItemType::Task: return "task";
        case ItemType::Bug: return "bug";
        case ItemType::Issue: return "issue";
    }
    return "item";
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
    diagnostics::ScopedMutationSpan total_span("workitem.create_item.total", title);
    CanonicalStore store(backlog_root);
    
    // 1. Generate ID and UID
    std::string type_code;
    switch (type) {
        case ItemType::Epic: type_code = "EPIC"; break;
        case ItemType::Feature: type_code = "FTR"; break;
        case ItemType::UserStory: type_code = "USR"; break;
        case ItemType::Task: type_code = "TSK"; break;
        case ItemType::Bug: type_code = "BUG"; break;
        case ItemType::Issue: type_code = "ISS"; break;
    }
    
    int number = 0;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.next_number", prefix + "-" + type_code);
        number = index.get_next_number(prefix, type_code);
    }
    
    // 2. Prepare item via store
    BacklogItem item;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.prepare");
        item = store.create(prefix, type, title, number, parent);
    }
    item.state = ItemState::Proposed;
    item.priority = priority;
    item.area = area;
    item.iteration = iteration;
    item.tags = tags;
    
    // 3. Render content using templates
    std::string content;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.render_template", item.id);
        content = TemplateOps::render_item_body(item, agent, "Created item");
    }
    
    // 4. Calculate path and write file
    // Note: CanonicalStore should have a way to calculate path without internal knowledge
    // For now we'll use a hack or implement it in store.
    std::filesystem::path item_path = *item.file_path;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.write_file", item.id);
        std::filesystem::create_directories(item_path.parent_path());
        std::ofstream ofs(item_path);
        ofs << content;
        ofs.close();
    }
    
    // 5. Update index
    item.file_path = item_path;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.index_item", item.id);
        index.index_item(item);
    }
    
    return {item.id, item.uid, item_path, type};
}

UpdateStateResult WorkitemOps::update_state(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    ItemState new_state,
    const std::string& agent,
    std::optional<std::string> message,
    bool force,
    bool refresh_views
) {
    diagnostics::ScopedMutationSpan total_span("workitem.update_state.total", item_ref);
    CanonicalStore store(backlog_root);
    BacklogItem item;
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.resolve_item", item_ref);
        item = resolve_item_or_throw(store, item_ref);
    }
    if (!item.file_path) {
        throw std::runtime_error("Resolved item has no file path: " + item_ref);
    }
    
    ItemState old_state = item.state;
    if (old_state == new_state) {
        return {item.id, old_state, new_state, false, false, false, {}};
    }

    std::vector<std::string> intent_diagnostics;
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.intent_diagnostics", item.id);
        intent_diagnostics = intent_transition_diagnostics(backlog_root, item, old_state, new_state);
    }

    // 3. Ready Gate Validation
    if (new_state == ItemState::InProgress && !force) {
        diagnostics::ScopedMutationSpan span("workitem.update_state.ready_validation", item.id);
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
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.transition", item.id);
        StateMachine::transition(item, action, agent, final_msg);
    }
    
    // 7. Parent sync logic
    bool parent_synced = false;
    if (item.parent) {
        diagnostics::ScopedMutationSpan span("workitem.update_state.parent_sync", item.id);
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
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.write_item", item.id);
        store.write(item);
    }

    bool dashboards_refreshed = false;
    if (refresh_views) {
        diagnostics::ScopedMutationSpan span("workitem.update_state.refresh_views", item.id);
        auto refreshed = ViewOps::refresh_dashboards(backlog_root, agent);
        dashboards_refreshed = !refreshed.views_refreshed.empty();
    }
    
    // 9. Update index
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.index_item", item.id);
        index.index_item(item);
    }
    
    return {item.id, old_state, new_state, true, parent_synced, dashboards_refreshed, intent_diagnostics};
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
    diagnostics::ScopedMutationSpan total_span("workitem.remap_parent.total", item_ref);
    CanonicalStore store(backlog_root);
    RefResolver resolver(store);
    BacklogItem item;
    {
        diagnostics::ScopedMutationSpan span("workitem.remap_parent.resolve_item", item_ref);
        item = resolver.resolve(item_ref);
    }

    // 2. Resolve parent
    if (new_parent_ref == "none" || new_parent_ref.empty() || new_parent_ref == "null") {
        item.parent = std::nullopt;
    } else {
        diagnostics::ScopedMutationSpan span("workitem.remap_parent.resolve_parent", new_parent_ref);
        BacklogItem parent_item = resolver.resolve(new_parent_ref);
        item.parent = parent_item.id;
    }

    // 3. Mark update and write
    StateMachine::record_worklog(item, agent, "Remapped parent to: " + item.parent.value_or("none"));
    {
        diagnostics::ScopedMutationSpan span("workitem.remap_parent.write_item", item.id);
        store.write(item);
    }
    
    // 4. Update index
    {
        diagnostics::ScopedMutationSpan span("workitem.remap_parent.index_item", item.id);
        index.index_item(item);
    }
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

IntentStackResult WorkitemOps::resolve_intent_stack(
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    int max_depth
) {
    if (max_depth < 1) {
        max_depth = 1;
    }
    if (looks_like_path_ref(item_ref)) {
        throw std::runtime_error("Intent stack item ref must be an item id or uid, not a path-like ref");
    }

    CanonicalStore store(backlog_root);
    RefResolver resolver(store);
    IntentStackResult result;
    std::set<std::string> seen;
    std::set<std::string> evidence_refs;

    BacklogItem current = resolver.resolve(item_ref);
    for (int depth = 0; depth < max_depth; ++depth) {
        const std::string identity = !current.uid.empty() ? current.uid : current.id;
        if (!seen.insert(identity).second) {
            result.warnings.push_back("Cycle detected at " + current.id + "; stopped parent-chain resolution");
            break;
        }

        result.chain.push_back({current, intent_stack_role_for_type(current.type), depth});
        for (const auto& ref : RefResolver::get_references(current)) {
            evidence_refs.insert(ref);
        }

        if (!current.parent || current.parent->empty() || *current.parent == "null" || *current.parent == "~") {
            break;
        }

        if (looks_like_path_ref(*current.parent)) {
            result.warnings.push_back("Parent ref for " + current.id + " is path-like and was not resolved: <redacted>");
            break;
        }

        try {
            current = resolver.resolve(*current.parent);
        } catch (const std::exception& ex) {
            result.warnings.push_back("Parent ref for " + current.id + " could not be resolved: " + *current.parent + " (" + ex.what() + ")");
            break;
        }
    }

    if (static_cast<int>(result.chain.size()) >= max_depth) {
        const auto& tail = result.chain.back().item;
        if (tail.parent && !tail.parent->empty() && *tail.parent != "null" && *tail.parent != "~") {
            result.warnings.push_back("Parent-chain depth limit reached at " + tail.id);
        }
    }

    result.evidence_refs.assign(evidence_refs.begin(), evidence_refs.end());
    return result;
}

} // namespace kano::backlog_ops
