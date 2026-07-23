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
#include <ctime>
#include <sstream>
#include <cctype>
#include <set>
#include <array>
#include <regex>
#include <string_view>

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

std::string trim_text(std::string value);

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

std::optional<std::string> read_first_line(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string line;
    if (!input.is_open() || !std::getline(input, line)) {
        return std::nullopt;
    }
    return trim_text(line);
}

struct SharedIdReservationContext {
    std::filesystem::path git_common_dir;
    std::filesystem::path repository_root;
    std::filesystem::path product_relative_path;
};

std::optional<SharedIdReservationContext> find_shared_id_reservation_context(
    const std::filesystem::path& backlog_root
) {
    auto cursor = normalize_path(backlog_root);
    while (!cursor.empty()) {
        const auto dot_git = cursor / ".git";
        std::error_code ec;
        std::filesystem::path git_dir;
        if (std::filesystem::is_directory(dot_git, ec)) {
            git_dir = dot_git;
        } else if (std::filesystem::is_regular_file(dot_git, ec)) {
            const auto line = read_first_line(dot_git);
            constexpr std::string_view prefix = "gitdir:";
            if (!line || !line->starts_with(prefix)) {
                return std::nullopt;
            }
            git_dir = normalize_path(cursor / trim_text(line->substr(prefix.size())));
        }

        if (!git_dir.empty()) {
            auto common_dir = git_dir;
            if (const auto common = read_first_line(git_dir / "commondir")) {
                common_dir = normalize_path(git_dir / *common);
            }
            std::error_code relative_error;
            auto product_relative = std::filesystem::relative(normalize_path(backlog_root), cursor, relative_error);
            if (relative_error || product_relative.empty() || product_relative.is_absolute()) {
                return std::nullopt;
            }
            return SharedIdReservationContext{normalize_path(common_dir), cursor, product_relative};
        }

        const auto parent = cursor.parent_path();
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> registered_product_roots(const SharedIdReservationContext& context) {
    std::set<std::filesystem::path> roots{normalize_path(context.repository_root / context.product_relative_path)};
    const auto worktrees_dir = context.git_common_dir / "worktrees";
    std::error_code ec;
    if (!std::filesystem::is_directory(worktrees_dir, ec)) {
        return {roots.begin(), roots.end()};
    }
    for (const auto& entry : std::filesystem::directory_iterator(worktrees_dir, ec)) {
        if (ec) break;
        const auto gitdir = read_first_line(entry.path() / "gitdir");
        if (!gitdir) continue;
        auto worktree_root = normalize_path(std::filesystem::path(*gitdir)).parent_path();
        auto product_root = normalize_path(worktree_root / context.product_relative_path);
        if (std::filesystem::is_directory(product_root, ec)) {
            roots.insert(product_root);
        }
        ec.clear();
    }
    return {roots.begin(), roots.end()};
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

void validate_ready_or_throw(const BacklogItem& item) {
    const auto [ready, gaps] = Validator::is_ready(item);
    if (ready) {
        return;
    }

    std::string gap_message;
    for (const auto& gap : gaps) {
        gap_message += (gap_message.empty() ? "" : ", ") + gap;
    }
    throw std::runtime_error("Item " + item.id + " is not Ready. Missing fields: " + gap_message);
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

bool has_unresolved_intent_alignment_evidence(const BacklogItem& item) {
    static constexpr std::array<std::string_view, 11> resolution_markers = {
        "intent alignment resolution:",
        "intent drift resolution:",
        "drift resolution:",
        "drift finding resolved",
        "drift resolved",
        "violation resolved",
        "no intent violation",
        "no do not violation",
        "no unresolved drift",
        "intent amendments resolved: ok",
        "warning is explicitly resolved"
    };
    static constexpr std::array<std::string_view, 2> structured_active_markers = {
        "intent drift finding:",
        "intent drift finding recorded"
    };
    static constexpr std::array<std::string_view, 5> amendment_active_markers = {
        "blocks done",
        "unresolved drift",
        "remains unresolved",
        "do not violation",
        "intent violation"
    };

    bool unresolved = false;
    const auto inspect_text = [&](const std::string& text, const bool is_intent_amendment) {
        for (const auto& evidence_line : split_evidence_lines(text)) {
            const auto line = lower_copy(evidence_line);
            const auto contains_any = [&](const auto& markers) {
                return std::any_of(markers.begin(), markers.end(), [&](std::string_view marker) {
                    return line.find(marker) != std::string::npos;
                });
            };

            // Evidence is chronological. An explicit resolution clears earlier findings,
            // including when the resolution line repeats the finding it closes.
            if (contains_any(resolution_markers)) {
                unresolved = false;
                continue;
            }
            const bool compliance_scope =
                line.find("do not compliance") != std::string::npos ||
                line.find("intent alignment") != std::string::npos;
            const bool explicit_violation_status = line.find(": violation") != std::string::npos;
            if (contains_any(structured_active_markers) || (compliance_scope && explicit_violation_status)) {
                unresolved = true;
                continue;
            }
            if (is_intent_amendment) {
                const auto trimmed = trim_text(line);
                const bool is_active_field =
                    trimmed.starts_with("reason:") || trimmed.starts_with("correction:");
                if (is_active_field && contains_any(amendment_active_markers)) {
                    unresolved = true;
                }
            }
        }
    };

    if (item.intent_amendments) {
        inspect_text(*item.intent_amendments, true);
    }
    for (const auto& entry : item.worklog) {
        inspect_text(entry, false);
    }
    return unresolved;
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

std::optional<std::size_t> find_evidence_key_position(
    const std::string& lowered,
    const std::string& key_pattern
) {
    std::size_t searchBegin = 0;
    while (searchBegin < lowered.size()) {
        const auto keyPos = lowered.find(key_pattern, searchBegin);
        if (keyPos == std::string::npos) {
            return std::nullopt;
        }
        if (keyPos == 0) {
            return keyPos;
        }
        const auto preceding = static_cast<unsigned char>(lowered[keyPos - 1]);
        if (std::isspace(preceding) ||
            lowered[keyPos - 1] == ';' ||
            lowered[keyPos - 1] == ',' ||
            lowered[keyPos - 1] == ':') {
            return keyPos;
        }
        searchBegin = keyPos + 1;
    }
    return std::nullopt;
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
        const auto keyPos = find_evidence_key_position(lowered, key_pattern);
        if (!keyPos) {
            continue;
        }
        auto value_begin = *keyPos + key_pattern.size();
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
    const auto keyPos = find_evidence_key_position(lowered, key_pattern);
    if (!keyPos) {
        return std::nullopt;
    }
    auto value_begin = *keyPos + key_pattern.size();
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

bool evidence_remote_published(const std::optional<std::string>& value) {
    if (!value) {
        return false;
    }
    if (evidence_yes(value)) {
        return true;
    }
    const auto normalized = lower_copy(trim_text(*value));
    if (normalized.empty() ||
        normalized.starts_with("pending") ||
        normalized == "false" ||
        normalized == "no" ||
        normalized == "none" ||
        normalized == "unknown" ||
        normalized == "unpublished" ||
        normalized == "local-only") {
        return false;
    }
    return normalized.find('/') != std::string::npos;
}

bool is_complete_branch_convergence_line(const std::string& line) {
    if (lower_copy(line).find("branch convergence:") == std::string::npos) {
        return false;
    }
    const bool hasTarget =
        extract_evidence_field(line, "target_branch").has_value() ||
        extract_evidence_field(line, "target").has_value();
    return hasTarget &&
        extract_evidence_field(line, "implementation_commit").has_value() &&
        evidence_yes(extract_evidence_field(line, "reachable_from_target")) &&
        evidence_remote_published(extract_evidence_field(line, "remote_publication"));
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
    for (const auto& text : lines) {
        for (const auto& line : split_evidence_lines(text)) {
            if (lower_copy(line).find("blocked convergence:") != std::string::npos) {
                evidence.present = true;
                evidence.branch = extract_evidence_field(line, "branch");
                evidence.reason = extract_evidence_field(line, "reason");
                evidence.next = extract_evidence_field(line, "next");
                evidence.blocker = extract_evidence_field(line, "blocker");
                continue;
            }
            if (evidence.present && is_complete_branch_convergence_line(line)) {
                evidence = {};
            }
        }
    }
    return evidence;
}

std::string value_or_missing(const std::optional<std::string>& value) {
    return value && !trim_text(*value).empty() ? trim_text(*value) : "<missing>";
}

bool has_metadata_text(const std::optional<std::string>& value) {
    return value && !trim_text(*value).empty();
}

bool is_non_implementation_intent(const BacklogItem& item) {
    if (!has_metadata_text(item.work_intent)) {
        return false;
    }
    return trim_text(*item.work_intent) != "implementation";
}

std::vector<std::string> missing_non_implementation_contract_fields(const BacklogItem& item) {
    std::vector<std::string> missing;
    if (!has_metadata_text(item.result_contract)) missing.push_back("result_contract");
    if (!has_metadata_text(item.evidence_requirement)) missing.push_back("evidence_requirement");
    if (!has_metadata_text(item.follow_up_policy)) missing.push_back("follow_up_policy");
    if (!has_metadata_text(item.no_go_or_defer_policy)) missing.push_back("no_go_or_defer_policy");
    return missing;
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

std::vector<std::string> trimmed_nonempty_values(const std::vector<std::string>& values) {
    std::vector<std::string> result;
    for (const auto& value : values) {
        auto trimmed = trim_text(value);
        if (!trimmed.empty()) result.push_back(trimmed);
    }
    return result;
}

bool contains_value(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string join_values(const std::vector<std::string>& values, const std::string& separator = ",") {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << separator;
        out << values[i];
    }
    return out.str();
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << static_cast<char>(ch); break;
        }
    }
    return out.str();
}

std::string json_array(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"" << json_escape(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

std::string current_utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

DuplicateAdmissionEvidence normalize_duplicate_admission(DuplicateAdmissionEvidence evidence) {
    evidence.search_query = trim_text(evidence.search_query);
    evidence.search_scope = trim_text(evidence.search_scope);
    evidence.decision = trim_text(evidence.decision);
    evidence.rationale = trim_text(evidence.rationale);
    evidence.candidates = trimmed_nonempty_values(evidence.candidates);
    evidence.candidates_read = trimmed_nonempty_values(evidence.candidates_read);
    return evidence;
}

void validate_duplicate_admission(const DuplicateAdmissionEvidence& evidence) {
    if (evidence.search_query.empty()) {
        throw std::runtime_error("duplicate_admission.search_query_required: pass --duplicate-search-query before creating a work item");
    }
    if (evidence.search_scope.empty()) {
        throw std::runtime_error("duplicate_admission.search_scope_required: pass --duplicate-search-scope before creating a work item");
    }
    if (evidence.decision.empty()) {
        throw std::runtime_error("duplicate_admission.decision_required: pass --duplicate-decision before creating a work item");
    }
    for (const auto& candidate : evidence.candidates) {
        if (!contains_value(evidence.candidates_read, candidate)) {
            throw std::runtime_error("duplicate_admission.candidate_unread: mark every --duplicate-candidate as read with --duplicate-candidate-read before creating");
        }
    }
    if (!evidence.candidates.empty()) {
        if (!evidence.override_requested) {
            throw std::runtime_error("duplicate_admission.override_required: pass --duplicate-override to create despite duplicate candidates");
        }
        if (evidence.rationale.empty()) {
            throw std::runtime_error("duplicate_admission.rationale_required: pass --duplicate-rationale when overriding duplicate candidates");
        }
    }
}

std::string duplicate_admission_worklog(const DuplicateAdmissionEvidence& evidence) {
    std::ostringstream out;
    out << "Duplicate admission: query=" << evidence.search_query
        << "; scope=" << evidence.search_scope
        << "; candidates=" << (evidence.candidates.empty() ? "none" : join_values(evidence.candidates))
        << "; read=" << (evidence.candidates_read.empty() ? "none" : join_values(evidence.candidates_read))
        << "; decision=" << evidence.decision
        << "; override=" << (evidence.override_requested ? "true" : "false");
    if (!evidence.rationale.empty()) out << "; rationale=" << evidence.rationale;
    return out.str();
}

void write_duplicate_admission_receipt(
    const std::filesystem::path& product_root,
    const BacklogItem& item,
    const DuplicateAdmissionEvidence& evidence
) {
    const auto canonical_product_root = std::filesystem::weakly_canonical(product_root);
    const auto receipt_dir = std::filesystem::weakly_canonical(product_root / "_meta") / "duplicate-admission";
    const auto relative_dir = receipt_dir.lexically_relative(canonical_product_root);
    if (relative_dir.empty() || *relative_dir.begin() == "..") {
        throw std::runtime_error("duplicate_admission.receipt_path_escape: duplicate admission receipt path escaped active product root");
    }
    std::filesystem::create_directories(receipt_dir);
    const auto receipt_path = receipt_dir / (item.id + ".json");
    const auto canonical_receipt_parent = std::filesystem::weakly_canonical(receipt_path.parent_path());
    const auto relative_receipt_parent = canonical_receipt_parent.lexically_relative(canonical_product_root);
    if (relative_receipt_parent.empty() || *relative_receipt_parent.begin() == "..") {
        throw std::runtime_error("duplicate_admission.receipt_path_escape: duplicate admission receipt path escaped active product root");
    }
    const auto temp_path = receipt_path.parent_path() / (item.id + ".json.tmp");
    std::ofstream out(temp_path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("duplicate_admission.receipt_write_failed: " + temp_path.string());
    }
    out << "{\n"
        << "  \"item_id\": \"" << json_escape(item.id) << "\",\n"
        << "  \"item_uid\": \"" << json_escape(item.uid) << "\",\n"
        << "  \"recorded_at\": \"" << current_utc_timestamp() << "\",\n"
        << "  \"search_query\": \"" << json_escape(evidence.search_query) << "\",\n"
        << "  \"search_scope\": \"" << json_escape(evidence.search_scope) << "\",\n"
        << "  \"candidates\": " << json_array(evidence.candidates) << ",\n"
        << "  \"candidates_read\": " << json_array(evidence.candidates_read) << ",\n"
        << "  \"decision\": \"" << json_escape(evidence.decision) << "\",\n"
        << "  \"override_requested\": " << (evidence.override_requested ? "true" : "false") << ",\n"
        << "  \"rationale\": \"" << json_escape(evidence.rationale) << "\"\n"
        << "}\n";
    if (!out.good()) {
        throw std::runtime_error("duplicate_admission.receipt_write_failed: " + temp_path.string());
    }
    out.close();
    std::filesystem::rename(temp_path, receipt_path);
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
        if (is_non_implementation_intent(item)) {
            const auto missing = missing_non_implementation_contract_fields(item);
            if (!missing.empty()) {
                diagnostics.push_back(
                    "InProgress->Review work intent contract: non-implementation work_intent=" +
                    trim_text(*item.work_intent) +
                    " missing " + join_values(missing, ", ") +
                    "; record result, evidence, follow-up, and no-go/defer policy before Review.");
            }
        }
    }

    if (old_state == ItemState::Review && new_state == ItemState::Done) {
        const bool has_unresolved_drift = has_unresolved_intent_alignment_evidence(item);
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
        case ItemType::Initiative: return "initiative";
        case ItemType::Epic: return "epic";
        case ItemType::Feature: return "feature";
        case ItemType::UserStory: return "story";
        case ItemType::Task: return "task";
        case ItemType::SubTask: return "subtask";
        case ItemType::Bug: return "bug";
        case ItemType::Issue: return "issue";
    }
    return "item";
}

bool is_parent_work_order_type(ItemType type) {
    return type == ItemType::Initiative || type == ItemType::Epic || type == ItemType::Feature;
}

bool is_implementation_work_order_type(ItemType type) {
    return type == ItemType::Task || type == ItemType::SubTask || type == ItemType::Bug || type == ItemType::Issue || type == ItemType::UserStory;
}

bool is_allowed_parent_work_order_intent(const std::string& intent) {
    static constexpr std::array<const char*, 7> allowed = {
        "decomposition",
        "planning",
        "decision",
        "docs_only",
        "policy_contract",
        "review_admission",
        "parent_reconciliation"
    };
    return std::any_of(allowed.begin(), allowed.end(), [&](const char* allowed_intent) {
        return intent == allowed_intent;
    });
}

bool is_ambiguous_work_order_intent(const std::string& intent) {
    return intent == "auto" || intent == "default" || intent == "unspecified" || intent == "unknown" || intent == "?";
}

bool is_source_changing_work_order_intent(const std::string& intent) {
    return intent == "implementation" || intent == "source_change" || intent == "source-changing" || intent == "code";
}

bool is_item_index_markdown(const std::filesystem::path& path) {
    return path.filename().generic_string().find(".index.md") != std::string::npos;
}

std::string candidate_recommendation_for(const BacklogItem& child) {
    if (is_implementation_work_order_type(child.type) && child.state == ItemState::Ready) {
        return "route_ready_child";
    }
    if (is_implementation_work_order_type(child.type) && child.state == ItemState::Proposed) {
        return "ready_gate_child";
    }
    return "consider_child";
}

std::vector<WorkOrderAdmissionCandidateChild> collect_work_order_candidate_children(
    const CanonicalStore& store,
    const BacklogItem& parent
) {
    std::vector<WorkOrderAdmissionCandidateChild> children;
    for (const auto& path : store.list_items()) {
        if (is_item_index_markdown(path)) {
            continue;
        }
        const auto child = store.read_metadata(path);
        if (!child.parent) {
            continue;
        }
        const auto parent_ref = trim_text(*child.parent);
        if (parent_ref != parent.id && parent_ref != parent.uid) {
            continue;
        }

        WorkOrderAdmissionCandidateChild candidate;
        candidate.id = child.id;
        candidate.type = to_string(child.type);
        candidate.state = to_string(child.state);
        candidate.title = child.title;
        candidate.work_intent = child.work_intent ? trim_text(*child.work_intent) : "";
        candidate.recommendation = candidate_recommendation_for(child);
        children.push_back(candidate);
    }
    std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return children;
}

bool has_candidate_recommendation(
    const std::vector<WorkOrderAdmissionCandidateChild>& children,
    const std::string& recommendation
) {
    return std::any_of(children.begin(), children.end(), [&](const auto& child) {
        return child.recommendation == recommendation;
    });
}

WorkOrderAdmissionResult make_admission_result(
    const BacklogItem& item,
    const std::string& requested_intent,
    const std::string& effective_intent
) {
    WorkOrderAdmissionResult result;
    result.item_id = item.id;
    result.item_type = to_string(item.type);
    result.requested_intent = requested_intent;
    result.effective_intent = effective_intent;
    result.starts_agent = false;
    result.dispatches_work = false;
    return result;
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

std::string diagnostic_path_list(
    const std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& product_root
) {
    if (paths.empty()) {
        return "<none>";
    }

    const std::size_t max_paths = 5;
    const std::size_t shown = std::min(paths.size(), max_paths);
    std::string result;
    for (std::size_t i = 0; i < shown; ++i) {
        if (i > 0) result += "; ";
        result += path_for_diagnostic(paths[i], product_root);
    }
    if (paths.size() > max_paths) {
        result += "; ... and " + std::to_string(paths.size() - max_paths) + " more";
    }
    return result;
}

std::vector<std::filesystem::path> item_paths_for_diagnostic(
    const std::vector<BacklogItem>& items
) {
    std::vector<std::filesystem::path> paths;
    for (const auto& item : items) {
        if (item.file_path) {
            paths.push_back(*item.file_path);
        }
    }
    return paths;
}

std::string item_id_type_code(const std::string& id) {
    const auto first_dash = id.find('-');
    if (first_dash == std::string::npos) {
        return {};
    }
    const auto second_dash = id.find('-', first_dash + 1);
    if (second_dash == std::string::npos) {
        return {};
    }
    return id.substr(first_dash + 1, second_dash - first_dash - 1);
}

void validate_remap_target_id(
    const CanonicalStore& store,
    const BacklogItem& item,
    const std::string& new_id,
    const std::filesystem::path& product_root
) {
    static const std::regex id_regex(R"(^[A-Z][A-Z0-9]{1,15}-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-\d{4}$)");
    if (!std::regex_match(new_id, id_regex)) {
        throw std::runtime_error(
            "duplicate_item_id.remap_target_invalid: new item id has invalid format: " +
            new_id + " (expected <PREFIX>-(INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-<NNNN>)");
    }
    if (new_id == item.id) {
        throw std::runtime_error("duplicate_item_id.remap_target_same: new item id matches current id: " + new_id);
    }
    if (item_id_type_code(new_id) != item_id_type_code(item.id)) {
        throw std::runtime_error(
            "duplicate_item_id.remap_target_type_mismatch: new item id type code must match current item type: " +
            item.id + " -> " + new_id);
    }

    const auto existing_target_paths = store.find_item_paths_by_id(new_id);
    if (!existing_target_paths.empty()) {
        throw std::runtime_error(
            "duplicate_item_id.remap_target_exists: target item id " + new_id +
            " already exists in active product root at " +
            diagnostic_path_list(existing_target_paths, product_root) +
            "; choose an unused id or repair duplicate bare IDs before retrying");
    }
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
            "duplicate_item_id.ambiguous_ref: parent item reference " +
            parent_ref_for_diagnostic(parent_ref) + " matched " +
            std::to_string(matches.size()) + " items: " +
            diagnostic_path_list(item_paths_for_diagnostic(matches), product_root) +
            "; use a UID/path-qualified recovery flow or repair duplicate bare IDs before retrying");
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

bool is_empty_parent_ref(const std::string& parent_ref) {
    const auto normalized = lower_copy(trim_text(parent_ref));
    return normalized.empty() || normalized == "none" || normalized == "null" || normalized == "~";
}

std::vector<ItemType> allowed_parent_types_for(ItemType child_type) {
    switch (child_type) {
        case ItemType::Initiative:
            return {};
        case ItemType::Epic:
            return {ItemType::Initiative};
        case ItemType::Feature:
            return {ItemType::Initiative, ItemType::Epic};
        case ItemType::UserStory:
            return {ItemType::Feature, ItemType::Epic};
        case ItemType::Task:
            return {ItemType::UserStory, ItemType::Feature, ItemType::Epic};
        case ItemType::SubTask:
            return {ItemType::Task};
        case ItemType::Bug:
        case ItemType::Issue:
            return {ItemType::Task, ItemType::Feature, ItemType::Epic, ItemType::Initiative};
    }
    return {};
}

std::string allowed_parent_types_text(ItemType child_type) {
    const auto allowed = allowed_parent_types_for(child_type);
    if (allowed.empty()) {
        return "no structural parent";
    }

    std::vector<std::string> names;
    names.reserve(allowed.size());
    for (const auto type : allowed) {
        names.push_back(to_string(type));
    }
    return join_values(names, ", ");
}

bool is_allowed_parent_type(ItemType child_type, ItemType parent_type) {
    const auto allowed = allowed_parent_types_for(child_type);
    return std::find(allowed.begin(), allowed.end(), parent_type) != allowed.end();
}

bool same_item_identity(const BacklogItem& left, const BacklogItem& right) {
    if (!left.id.empty() && left.id == right.id) {
        return true;
    }
    return !left.uid.empty() && left.uid == right.uid;
}

void validate_parent_relationship(
    const RefResolver& resolver,
    const BacklogItem& item,
    const BacklogItem& parent_item
) {
    if (!is_allowed_parent_type(item.type, parent_item.type)) {
        throw std::runtime_error(
            "hierarchy.parent_type_invalid: " + to_string(item.type) +
            " parent must be " + allowed_parent_types_text(item.type) +
            "; got " + to_string(parent_item.type) + " (" + parent_item.id + ")");
    }

    std::set<std::string> seen;
    BacklogItem current = parent_item;
    while (true) {
        if (same_item_identity(current, item)) {
            throw std::runtime_error(
                "hierarchy.parent_cycle: proposed parent " + parent_item.id +
                " is the item itself or a descendant of " + item.id);
        }

        const std::string current_identity = !current.uid.empty() ? current.uid : current.id;
        if (!seen.insert(current_identity).second) {
            throw std::runtime_error(
                "hierarchy.parent_cycle: proposed parent chain already contains a cycle at " + current.id);
        }

        if (!current.parent || is_empty_parent_ref(*current.parent)) {
            return;
        }

        try {
            current = resolver.resolve(*current.parent);
        } catch (const std::exception& ex) {
            throw std::runtime_error(
                "hierarchy.parent_cycle_unverified: cannot verify proposed parent chain at " +
                current.id + " parent " + parent_ref_for_diagnostic(*current.parent) +
                " (" + ex.what() + ")");
        }
    }
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
    std::string iteration,
    std::optional<std::string> owner,
    std::optional<std::string> reviewer,
    std::string owner_source,
    std::string reviewer_source,
    DuplicateAdmissionEvidence duplicate_admission
) {
    diagnostics::ScopedMutationSpan total_span("workitem.create_item.total", title);
    CanonicalStore store(backlog_root);
    auto normalized_duplicate_admission = normalize_duplicate_admission(std::move(duplicate_admission));
    validate_duplicate_admission(normalized_duplicate_admission);
    
    // 1. Generate ID and UID
    std::string type_code;
    switch (type) {
        case ItemType::Initiative: type_code = "INIT"; break;
        case ItemType::Epic: type_code = "EPIC"; break;
        case ItemType::Feature: type_code = "FTR"; break;
        case ItemType::UserStory: type_code = "USR"; break;
        case ItemType::Task: type_code = "TSK"; break;
        case ItemType::SubTask: type_code = "SUBTSK"; break;
        case ItemType::Bug: type_code = "BUG"; break;
        case ItemType::Issue: type_code = "ISS"; break;
    }
    
    if (!index.has_sequence(prefix, type_code)) {
        diagnostics::ScopedMutationSpan span("workitem.create_item.seed_sequence", prefix + "-" + type_code);
        index.ensure_sequence_at_least(prefix, type_code, store.get_max_id_number(prefix, type));
    }

    std::optional<BacklogIndex> shared_reservations;
    if (const auto reservation_context = find_shared_id_reservation_context(backlog_root)) {
        diagnostics::ScopedMutationSpan span("workitem.create_item.seed_shared_reservation", prefix + "-" + type_code);
        const auto reservation_db = reservation_context->git_common_dir / "kano" / "backlog-id-reservations.db";
        shared_reservations.emplace(reservation_db);
        shared_reservations->initialize();
        int registered_max = 0;
        for (const auto& product_root : registered_product_roots(*reservation_context)) {
            registered_max = std::max(
                registered_max,
                CanonicalStore(product_root).get_max_id_number(prefix, type));
        }
        shared_reservations->ensure_sequence_at_least(prefix, type_code, registered_max);
    }

    BacklogItem item;
    int reserved_number = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int number = 0;
        {
            diagnostics::ScopedMutationSpan span("workitem.create_item.next_number", prefix + "-" + type_code);
            number = shared_reservations
                ? shared_reservations->reserve_next_number(prefix, type_code, agent)
                : index.get_next_number(prefix, type_code);
            reserved_number = number;
            index.ensure_sequence_at_least(prefix, type_code, number);
        }
        {
            diagnostics::ScopedMutationSpan span("workitem.create_item.prepare");
            item = store.create(prefix, type, title, number, parent);
        }
        if (!item.file_path) {
            throw std::runtime_error("duplicate_item_id.path_missing: generated item has no target file path");
        }

        diagnostics::ScopedMutationSpan span("workitem.create_item.collision_check", item.id);
        const auto existing_paths = store.find_item_paths_by_id(item.id);
        std::error_code exists_error;
        const bool path_exists = std::filesystem::exists(*item.file_path, exists_error);
        if (existing_paths.empty() && !path_exists) {
            break;
        }
        if (attempt == 0) {
            const int canonical_max = store.get_max_id_number(prefix, type);
            index.ensure_sequence_at_least(prefix, type_code, canonical_max);
            if (shared_reservations) {
                shared_reservations->ensure_sequence_at_least(prefix, type_code, canonical_max);
            }
            continue;
        }
        if (!existing_paths.empty()) {
            throw std::runtime_error(
                "duplicate_item_id.allocation_collision: allocated item id " + item.id +
                " already exists in active product root at " +
                diagnostic_path_list(existing_paths, backlog_root) +
                "; repair/remap the existing item by UID before retrying");
        }
        throw std::runtime_error(
            "duplicate_item_id.path_collision: generated item path already exists: " +
            path_for_diagnostic(*item.file_path, backlog_root));
    }
    item.state = ItemState::Proposed;
    item.priority = priority;
    item.area = area;
    item.iteration = iteration;
    item.tags = tags;
    if (owner && !owner->empty()) {
        item.owner = *owner;
        if (!owner_source.empty()) {
            item.external["owner_source"] = owner_source;
        }
    }
    if (reviewer && !reviewer->empty()) {
        item.external["reviewer"] = *reviewer;
        if (!reviewer_source.empty()) {
            item.external["reviewer_source"] = reviewer_source;
        }
    }
    
    // 3. Render content using templates
    std::string content;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.render_template", item.id);
        content = TemplateOps::render_item_body(item, agent, "Created item; " + duplicate_admission_worklog(normalized_duplicate_admission));
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
    if (shared_reservations) {
        shared_reservations->commit_reservation(prefix, type_code, reserved_number);
    }
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.duplicate_admission_receipt", item.id);
        write_duplicate_admission_receipt(backlog_root, item, normalized_duplicate_admission);
    }
    
    // 5. Update index
    item.file_path = item_path;
    {
        diagnostics::ScopedMutationSpan span("workitem.create_item.index_item", item.id);
        index.index_item(item);
    }
    
    return {item.id, item.uid, item_path, type};
}

BacklogItem WorkitemOps::transition_state_action(
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    StateAction action,
    std::optional<std::string> agent,
    std::optional<std::string> message,
    std::optional<std::string> model
) {
    diagnostics::ScopedMutationSpan total_span("workitem.transition_state_action.total", item_ref);
    CanonicalStore store(backlog_root);
    BacklogItem item;
    {
        diagnostics::ScopedMutationSpan span("workitem.transition_state_action.resolve_item", item_ref);
        item = resolve_item_or_throw(store, item_ref);
    }

    if (action == StateAction::Start) {
        diagnostics::ScopedMutationSpan span("workitem.transition_state_action.ready_validation", item.id);
        validate_ready_or_throw(item);
    }

    {
        diagnostics::ScopedMutationSpan span("workitem.transition_state_action.transition", item.id);
        StateMachine::transition(item, action, agent, message, model);
    }
    {
        diagnostics::ScopedMutationSpan span("workitem.transition_state_action.write_item", item.id);
        store.write(item);
    }
    return item;
}

UpdateStateResult WorkitemOps::update_state(
    BacklogIndex& index,
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    ItemState new_state,
    const std::string& agent,
    std::optional<std::string> message,
    std::optional<std::string> duplicate_of,
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

    StateAction action;
    switch (new_state) {
        case ItemState::Proposed: action = StateAction::Propose; break;
        case ItemState::Planned: // Fallthrough
        case ItemState::Ready: action = StateAction::Ready; break;
        case ItemState::InProgress:
            action = old_state == ItemState::Review ? StateAction::Reopen : StateAction::Start;
            break;
        case ItemState::Review: action = StateAction::Review; break;
        case ItemState::Done: action = StateAction::Done; break;
        case ItemState::Blocked: action = StateAction::Block; break;
        case ItemState::Dropped: action = StateAction::Drop; break;
        case ItemState::Duplicate: action = StateAction::Duplicate; break;
        case ItemState::New:
            throw std::runtime_error("Cannot update item state to New");
    }

    if (new_state == ItemState::Duplicate) {
        if (!duplicate_of || trim_text(*duplicate_of).empty()) {
            throw std::runtime_error("Transition to Duplicate requires --duplicate-of <canonical-item-ref>");
        }
        RefResolver resolver(store);
        const BacklogItem canonical_item = resolver.resolve(trim_text(*duplicate_of));
        if (canonical_item.id == item.id || canonical_item.uid == item.uid) {
            throw std::runtime_error("Duplicate item cannot point duplicate_of at itself: " + item.id);
        }
        item.duplicate_of = canonical_item.id;
    } else if (duplicate_of && !trim_text(*duplicate_of).empty()) {
        throw std::runtime_error("--duplicate-of is only valid when transitioning to Duplicate");
    }

    std::vector<std::string> intent_diagnostics;
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.intent_diagnostics", item.id);
        BacklogItem diagnostic_item = item;
        if (message && !trim_text(*message).empty()) {
            diagnostic_item.worklog.push_back(*message);
        }
        intent_diagnostics = intent_transition_diagnostics(backlog_root, diagnostic_item, old_state, new_state);
    }

    // 3. Ready Gate Validation
    if (new_state == ItemState::InProgress && !force) {
        diagnostics::ScopedMutationSpan span("workitem.update_state.ready_validation", item.id);
        validate_ready_or_throw(item);

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
    
    std::string final_msg = message.value_or("State update: " + to_string(old_state) + " -> " + to_string(new_state));
    if (action == StateAction::Start) {
        final_msg += force ? " [Ready gate bypassed via --force]" : " [Ready gate validated]";
    }
    if (new_state == ItemState::Duplicate && item.duplicate_of) {
        final_msg += " [duplicate_of=" + *item.duplicate_of + "]";
    }

    // 6. Transition via StateMachine
    {
        diagnostics::ScopedMutationSpan span("workitem.update_state.transition", item.id);
        if (action == StateAction::Reopen) {
            std::optional<std::string> reopen_message = message;
            if (message && !trim_text(*message).empty()) {
                reopen_message = *message +
                    (force ? " [Ready gate bypassed via --force]" : " [Ready gate validated]");
            }
            StateMachine::transition(item, action, agent, reopen_message);
        } else {
            StateMachine::transition(item, action, agent, final_msg);
        }
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
    if (is_empty_parent_ref(new_parent_ref)) {
        item.parent = std::nullopt;
    } else {
        diagnostics::ScopedMutationSpan span("workitem.remap_parent.resolve_parent", new_parent_ref);
        BacklogItem parent_item = resolver.resolve(new_parent_ref);
        validate_parent_relationship(resolver, item, parent_item);
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
    validate_remap_target_id(store, item, new_id, backlog_root);

    std::filesystem::path old_path = *item.file_path;
    std::string old_id = item.id;
    const auto old_id_paths = store.find_item_paths_by_id(old_id);
    const bool duplicate_source_id = old_id_paths.size() > 1;
    
    // 2. Calculate new path
    std::string filename = old_path.filename().string();
    size_t underscore_pos = filename.find('_');
    std::string tail = (underscore_pos != std::string::npos) ? filename.substr(underscore_pos) : ".md";
    std::filesystem::path new_path = old_path.parent_path() / (new_id + tail);
    const auto old_index_path = old_path.parent_path() / (old_path.stem().string() + ".index.md");
    const auto new_index_path = new_path.parent_path() / (new_path.stem().string() + ".index.md");
    const bool has_adjacent_index = std::filesystem::exists(old_index_path);
    std::error_code exists_error;
    if (old_path != new_path && std::filesystem::exists(new_path, exists_error)) {
        throw std::runtime_error(
            "duplicate_item_id.remap_path_collision: target item path already exists: " +
            path_for_diagnostic(new_path, backlog_root));
    }
    if (has_adjacent_index && old_index_path != new_index_path &&
        std::filesystem::exists(new_index_path, exists_error)) {
        throw std::runtime_error(
            "duplicate_item_id.remap_index_path_collision: target adjacent index path already exists: " +
            path_for_diagnostic(new_index_path, backlog_root));
    }
    
    // 3. Update ID in item and add worklog
    item.id = new_id;
    StateMachine::record_worklog(item, agent, "Remapped ID: " + old_id + " -> " + new_id);
    
    // 4. Rename file
    if (old_path != new_path) {
        std::filesystem::create_directories(new_path.parent_path());
        std::filesystem::rename(old_path, new_path);
    }
    if (has_adjacent_index && old_index_path != new_index_path) {
        std::filesystem::rename(old_index_path, new_index_path);
    }
    
    // 5. Write back with new ID
    item.file_path = new_path;
    store.write(item);
    
    // 6. Global search and replace in the product root
    int updated_files = 1;
    
    if (!duplicate_source_id) {
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
    }
    
    // 7. Update index
    if (!duplicate_source_id) {
        index.remove_item(old_id);
    } else {
        for (const auto& peer_path : old_id_paths) {
            if (peer_path == old_path) continue;
            try {
                index.index_item(store.read(peer_path));
            } catch (...) {
                // Canonical files remain authoritative; a later index refresh
                // can repair a peer that became unreadable independently.
            }
        }
    }
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

WorkOrderAdmissionResult WorkitemOps::evaluate_work_order_admission(
    const std::filesystem::path& backlog_root,
    const std::string& item_ref,
    std::optional<std::string> requested_intent,
    bool source_changing_hint
) {
    if (looks_like_path_ref(item_ref)) {
        throw std::runtime_error("Work-order admission item ref must be an item id or uid, not a path-like ref");
    }

    CanonicalStore store(backlog_root);
    RefResolver resolver(store);
    const auto item = resolver.resolve(item_ref);

    const std::string requested = requested_intent ? lower_copy(trim_text(*requested_intent)) : "";
    std::string effective = requested;
    if (effective.empty() && !is_parent_work_order_type(item.type) && item.work_intent && !trim_text(*item.work_intent).empty()) {
        effective = lower_copy(trim_text(*item.work_intent));
    }
    if (effective.empty() && is_implementation_work_order_type(item.type)) {
        effective = "implementation";
    }

    auto result = make_admission_result(item, requested, effective);

    if (is_parent_work_order_type(item.type)) {
        result.candidate_children = collect_work_order_candidate_children(store, item);
        if (requested.empty() || is_ambiguous_work_order_intent(requested)) {
            result.admitted = false;
            result.requires_explicit_intent = true;
            result.reason_code = "parent_explicit_intent_required";
            result.message = "Parent item work-order admission requires an explicit work_intent such as decomposition, planning, decision, docs_only, policy_contract, review_admission, or parent_reconciliation; missing, blank, and ambiguous intents are blocked.";
            return result;
        }

        if (requested == "parent_reconciliation" && source_changing_hint) {
            result.admitted = false;
            result.reason_code = "parent_reconciliation_source_changes_blocked";
            result.message = "Parent reconciliation is admitted only for evidence, worklog, or state-summary reconciliation; source implementation must be routed to executable child work.";
            return result;
        }

        if (is_source_changing_work_order_intent(requested) || source_changing_hint) {
            result.admitted = false;
            if (has_candidate_recommendation(result.candidate_children, "route_ready_child")) {
                result.reason_code = "parent_implementation_blocked_ready_child";
                result.message = "Parent implementation/source-changing dispatch is blocked; route the work to a Ready child item instead.";
            } else if (has_candidate_recommendation(result.candidate_children, "ready_gate_child")) {
                result.reason_code = "parent_implementation_blocked_ready_gate_child";
                result.message = "Parent implementation/source-changing dispatch is blocked; move a proposed child item through the Ready gate before execution.";
            } else if (!result.candidate_children.empty()) {
                result.reason_code = "parent_implementation_blocked_candidate_children";
                result.message = "Parent implementation/source-changing dispatch is blocked; review candidate child items and route executable work to a child.";
            } else {
                result.reason_code = "parent_implementation_blocked_decompose";
                result.message = "Parent implementation/source-changing dispatch is blocked and no child items exist; decompose the parent into executable child work before implementation.";
            }
            return result;
        }

        if (!is_allowed_parent_work_order_intent(requested)) {
            result.admitted = false;
            result.reason_code = "parent_intent_not_allowed";
            result.message = "Parent item intent is not admitted for work-order dispatch; use decomposition, planning, decision, docs_only, policy_contract, review_admission, or parent_reconciliation.";
            return result;
        }

        if (requested == "parent_reconciliation") {
            result.admitted = true;
            result.reason_code = "parent_reconciliation_allowed";
            result.message = "Parent reconciliation is admitted only for evidence, worklog, or state-summary reconciliation; source implementation remains blocked.";
        } else {
            result.admitted = true;
            result.reason_code = "parent_intent_allowed";
            result.message = "Parent item work-order admission is allowed for non-source-changing intent: " + requested + ".";
        }
        result.would_create_work_order = true;
        result.would_dispatch = true;
        return result;
    }

    if (is_ambiguous_work_order_intent(effective)) {
        result.admitted = false;
        result.requires_explicit_intent = true;
        result.reason_code = "explicit_intent_required";
        result.message = "Work-order admission requires an explicit non-ambiguous work_intent.";
        return result;
    }

    if (is_implementation_work_order_type(item.type)) {
        result.admitted = true;
        result.reason_code = "item_intent_allowed";
        result.message = "Work-order admission is allowed for executable item type " + to_string(item.type) + " with intent " + result.effective_intent + ".";
        result.would_create_work_order = true;
        result.would_dispatch = true;
        return result;
    }

    result.admitted = false;
    result.reason_code = "item_type_not_supported";
    result.message = "Work-order admission does not support this item type.";
    return result;
}

} // namespace kano::backlog_ops
