#include "kano/backlog_ops/workset/workset_ops.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cctype>

namespace kano::backlog_ops {
using namespace kano::backlog_core;
namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;       break;
        }
    }
    return out;
}

void json_write_string(std::ostream& os, const std::string& key, const std::string& value) {
    os << "    \"" << json_escape(key) << "\": \"" << json_escape(value) << "\"";
}

void json_write_int(std::ostream& os, const std::string& key, int value) {
    os << "    \"" << json_escape(key) << "\": " << value;
}

void json_write_opt_string(std::ostream& os, const std::string& key, const std::optional<std::string>& value) {
    os << "    \"" << json_escape(key) << "\": ";
    if (value && !value->empty()) {
        os << "\"" << json_escape(*value) << "\"";
    } else {
        os << "null";
    }
}

std::string current_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::optional<WorksetOps::WorksetMetadata> parse_meta(const std::string& content) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::istringstream input(content);
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors) || !root.isObject()) {
        return std::nullopt;
    }

    WorksetOps::WorksetMetadata meta;
    meta.workset_id = root.get("workset_id", "").asString();
    meta.item_id = root.get("item_id", "").asString();
    meta.item_uid = root.get("item_uid", "").asString();
    meta.item_path = root.get("item_path", "").asString();
    meta.agent = root.get("agent", "").asString();
    meta.created_at = root.get("created_at", "").asString();
    meta.refreshed_at = root.get("refreshed_at", "").asString();
    meta.ttl_hours = root.get("ttl_hours", 72).asInt();
    if (root.isMember("source_commit") && root["source_commit"].isString()) {
        meta.source_commit = root["source_commit"].asString();
    }

    if (meta.workset_id.empty()) return std::nullopt;
    return meta;
}

struct ResolvedItemContext {
    std::filesystem::path product_root;
    BacklogItem item;
};

std::optional<std::filesystem::path> infer_product_root_from_item_path(
    const std::filesystem::path& item_path,
    const std::filesystem::path& backlog_root
) {
    if (!std::filesystem::exists(item_path)) {
        return std::nullopt;
    }

    for (auto current = item_path.parent_path(); !current.empty(); current = current.parent_path()) {
        if (current.filename() == "items") {
            auto product_root = current.parent_path();
            if (!product_root.empty()) {
                return product_root;
            }
        }
        if (current == current.root_path()) {
            break;
        }
    }

    if (std::filesystem::exists(backlog_root / "items") &&
        item_path.string().find((backlog_root / "items").string()) == 0) {
        return backlog_root;
    }

    return std::nullopt;
}

std::optional<ResolvedItemContext> resolve_item_context(
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    if (item_ref.find('/') != std::string::npos ||
        item_ref.find('\\') != std::string::npos ||
        item_ref.find(".md") != std::string::npos) {
        std::filesystem::path item_path(item_ref);
        if (!item_path.is_absolute()) {
            item_path = (backlog_root / item_path).lexically_normal();
        }
        if (std::filesystem::exists(item_path)) {
            auto product_root = infer_product_root_from_item_path(item_path, backlog_root);
            if (product_root) {
                CanonicalStore store(*product_root);
                return ResolvedItemContext{*product_root, store.read(item_path)};
            }
        }
    }

    std::vector<std::filesystem::path> candidate_product_roots;
    if (std::filesystem::exists(backlog_root / "items")) {
        candidate_product_roots.push_back(backlog_root);
    }

    auto products_dir = backlog_root / "products";
    if (std::filesystem::exists(products_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(products_dir)) {
            if (entry.is_directory() && std::filesystem::exists(entry.path() / "items")) {
                candidate_product_roots.push_back(entry.path());
            }
        }
    }

    for (const auto& product_root : candidate_product_roots) {
        CanonicalStore store(product_root);
        RefResolver resolver(store);
        auto item = resolver.resolve_or_none(item_ref);
        if (item) {
            return ResolvedItemContext{product_root, *item};
        }
    }

    return std::nullopt;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// WorksetOps implementation
// ---------------------------------------------------------------------------

std::filesystem::path workset_root(const std::filesystem::path& backlog_root) {
    return backlog_root / ".cache" / "worksets" / "items";
}

std::optional<std::filesystem::path> WorksetOps::resolve_workset_path(
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    auto root = workset_root(backlog_root);
    if (!std::filesystem::exists(root)) return std::nullopt;

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        auto meta = load_meta(entry.path());
        if (!meta) continue;
        if (meta->item_id == item_ref || meta->item_uid == item_ref ||
            meta->item_path == item_ref || meta->workset_id == item_ref ||
            entry.path().filename().string() == item_ref) {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::optional<WorksetOps::WorksetMetadata> WorksetOps::load_meta(
    const std::filesystem::path& workset_path
) {
    auto meta_path = workset_path / "meta.json";
    if (!std::filesystem::exists(meta_path)) return std::nullopt;

    std::ifstream ifs(meta_path);
    if (!ifs.is_open()) return std::nullopt;

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return parse_meta(buffer.str());
}

void WorksetOps::save_meta(
    const std::filesystem::path& workset_path,
    const WorksetMetadata& meta
) {
    auto meta_path = workset_path / "meta.json";
    std::ofstream ofs(meta_path);
    if (!ofs.is_open()) return;

    ofs << "{\n";
    json_write_string(ofs, "workset_id", meta.workset_id); ofs << ",\n";
    json_write_string(ofs, "item_id", meta.item_id); ofs << ",\n";
    json_write_string(ofs, "item_uid", meta.item_uid); ofs << ",\n";
    json_write_string(ofs, "item_path", meta.item_path); ofs << ",\n";
    json_write_string(ofs, "agent", meta.agent); ofs << ",\n";
    json_write_string(ofs, "created_at", meta.created_at); ofs << ",\n";
    json_write_string(ofs, "refreshed_at", meta.refreshed_at); ofs << ",\n";
    json_write_int(ofs, "ttl_hours", meta.ttl_hours); ofs << ",\n";
    json_write_opt_string(ofs, "source_commit", meta.source_commit);
    ofs << "\n}\n";
}

WorksetOps::WorksetInitResult WorksetOps::init_workset(
    const std::string& item_ref,
    const std::string& agent,
    const std::filesystem::path& backlog_root,
    int ttl_hours
) {
    auto resolved = resolve_item_context(item_ref, backlog_root);
    if (!resolved) {
        throw std::runtime_error("Item not found for workset: " + item_ref);
    }

    auto item = resolved->item;
    const std::string canonical_item_id = item.id;
    const std::string canonical_item_uid = item.uid;
    const std::string canonical_item_path = item.file_path ? item.file_path->string() : item_ref;

    // Check if workset already exists
    auto existing = resolve_workset_path(canonical_item_id, backlog_root);
    bool created = false;

    WorksetMetadata meta;
    std::filesystem::path workset_path;

    if (!existing) {
        // Create new workset
        auto root = workset_root(backlog_root);
        std::filesystem::create_directories(root);

        // Canonical layout: items/<ITEM_ID>/
        std::string ws_id = canonical_item_id;
        workset_path = root / ws_id;
        std::filesystem::create_directories(workset_path);
        std::filesystem::create_directories(workset_path / "deliverables");

        meta.workset_id = ws_id;
        meta.item_id = canonical_item_id;
        meta.item_uid = canonical_item_uid;
        meta.item_path = canonical_item_path;
        meta.agent = agent;
        meta.created_at = current_iso_timestamp();
        meta.refreshed_at = meta.created_at;
        meta.ttl_hours = ttl_hours;

        save_meta(workset_path, meta);

        // Create plan.md (empty checklist)
        std::ofstream plan_ofs(workset_path / "plan.md");
        plan_ofs << "# Plan\n\n";
        if (item.acceptance_criteria && !trim(*item.acceptance_criteria).empty()) {
            std::istringstream criteria_stream(*item.acceptance_criteria);
            std::string criteria_line;
            bool wrote_any = false;
            while (std::getline(criteria_stream, criteria_line)) {
                auto text = trim(criteria_line);
                if (text.empty()) continue;
                if (text.rfind("- ", 0) == 0 || text.rfind("* ", 0) == 0) {
                    text = trim(text.substr(2));
                }
                plan_ofs << "- [ ] " << text << "\n";
                wrote_any = true;
            }
            if (!wrote_any) {
                plan_ofs << "- [ ] " << trim(*item.acceptance_criteria) << "\n";
            }
        } else {
            plan_ofs << "- [ ] Review acceptance criteria\n";
            plan_ofs << "- [ ] Implement required changes\n";
            plan_ofs << "- [ ] Verify results\n";
        }

        // Create notes.md
        std::ofstream notes_ofs(workset_path / "notes.md");
        notes_ofs << "# Notes\n\n";
        notes_ofs << "Working notes for " << canonical_item_id << "\n\n";
        notes_ofs << "Decision: \n";

        StateMachine::record_worklog(item, agent, "Initialized workset: " + workset_path.string());
        CanonicalStore store(resolved->product_root);
        store.write(item);

        created = true;
    } else {
        workset_path = *existing;
        auto loaded = load_meta(workset_path);
        if (loaded) meta = *loaded;
    }

    return {workset_path, 1, created};
}

WorksetOps::WorksetNextResult WorksetOps::get_next_action(
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    auto ws_path = resolve_workset_path(item_ref, backlog_root);
    if (!ws_path) {
        throw std::runtime_error("Workset not found for item: " + item_ref);
    }

    auto plan_path = *ws_path / "plan.md";
    if (!std::filesystem::exists(plan_path)) {
        return {0, "(no plan.md found)", true};
    }

    std::ifstream ifs(plan_path);
    if (!ifs.is_open()) {
        return {0, "(cannot read plan.md)", true};
    }

    std::string line;
    int step_number = 0;
    bool is_complete = true;
    std::string next_description;

    std::regex checkbox_re(R"(^\s*-\s*\[([ xX])\]\s*(.+)$)");

    while (std::getline(ifs, line)) {
        std::smatch match;
        if (std::regex_match(line, match, checkbox_re)) {
            step_number++;
            std::string state = match[1].str();
            std::string desc = trim(match[2].str());

            if ((state == " " || state == "[ ]") && next_description.empty()) {
                next_description = desc;
                is_complete = false;
                break;
            }
        }
    }

    if (is_complete) {
        return {0, "(all steps complete)", true};
    }
    return {step_number, next_description, false};
}

WorksetOps::WorksetRefreshResult WorksetOps::refresh_workset(
    const std::string& item_ref,
    const std::string& agent,
    const std::filesystem::path& backlog_root
) {
    auto ws_path = resolve_workset_path(item_ref, backlog_root);
    if (!ws_path) {
        throw std::runtime_error("Workset not found for item: " + item_ref);
    }

    auto meta = load_meta(*ws_path);
    if (meta) {
        auto resolved = resolve_item_context(meta->item_uid.empty() ? item_ref : meta->item_uid, backlog_root);
        if (!resolved) {
            throw std::runtime_error("Source item not found for workset: " + item_ref);
        }

        auto item = resolved->item;
        meta->item_id = item.id;
        meta->item_uid = item.uid;
        meta->item_path = item.file_path ? item.file_path->string() : meta->item_path;
        meta->refreshed_at = current_iso_timestamp();
        save_meta(*ws_path, *meta);

        StateMachine::record_worklog(item, agent, "Refreshed workset: " + ws_path->string());
        CanonicalStore store(resolved->product_root);
        store.write(item);
    }

    return {*ws_path, 0, 0, 1};
}

WorksetOps::WorksetPromoteResult WorksetOps::promote_deliverables(
    const std::string& item_ref,
    const std::string& agent,
    const std::filesystem::path& backlog_root,
    bool dry_run
) {
    auto ws_path = resolve_workset_path(item_ref, backlog_root);
    if (!ws_path) {
        throw std::runtime_error("Workset not found for item: " + item_ref);
    }

    auto deliverables_path = *ws_path / "deliverables";
    if (!std::filesystem::exists(deliverables_path)) {
        return {{}, {}, ""};
    }

    auto meta = load_meta(*ws_path);
    if (!meta) return {{}, {}, ""};

    // Target: _kano/backlog/products/<product>/artifacts/<item_id>/
    auto resolved = resolve_item_context(meta->item_uid.empty() ? meta->item_id : meta->item_uid, backlog_root);
    if (!resolved) {
        throw std::runtime_error("Source item not found for workset promotion: " + item_ref);
    }
    auto target_path = (resolved->product_root / "artifacts" / meta->item_id).lexically_normal();

    std::vector<std::string> promoted;
    if (!dry_run) {
        std::filesystem::create_directories(target_path);
    }

    for (const auto& entry : std::filesystem::directory_iterator(deliverables_path)) {
        if (!entry.is_regular_file()) continue;
        promoted.push_back(entry.path().filename().string());
        if (!dry_run) {
            std::filesystem::copy_file(entry.path(), target_path / entry.path().filename(),
                std::filesystem::copy_options::overwrite_existing);
        }
    }

    std::string worklog_entry = current_iso_timestamp() + " [" + agent + "] Promoted " +
        std::to_string(promoted.size()) + " deliverables to artifacts";

    if (!dry_run && !promoted.empty()) {
        auto item = resolved->item;
        StateMachine::record_worklog(item, agent, worklog_entry);
        CanonicalStore store(resolved->product_root);
        store.write(item);
    }

    return {promoted, target_path, worklog_entry};
}

std::vector<WorksetOps::WorksetMetadata> WorksetOps::list_worksets(
    const std::filesystem::path& backlog_root
) {
    std::vector<WorksetMetadata> result;
    auto root = workset_root(backlog_root);

    if (!std::filesystem::exists(root)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        auto meta = load_meta(entry.path());
        if (meta) result.push_back(*meta);
    }

    std::sort(result.begin(), result.end(),
        [](const WorksetMetadata& a, const WorksetMetadata& b) {
            return a.created_at > b.created_at;
        });

    return result;
}

WorksetOps::WorksetCleanupResult WorksetOps::cleanup_worksets(
    const std::filesystem::path& backlog_root,
    int ttl_hours,
    bool dry_run
) {
    auto root = workset_root(backlog_root);
    if (!std::filesystem::exists(root)) {
        return {0, {}, 0};
    }

    auto now = std::chrono::system_clock::now();
    std::vector<std::filesystem::path> deleted_paths;
    size_t space_reclaimed = 0;
    int deleted_count = 0;

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        auto meta = load_meta(entry.path());
        if (!meta) continue;

        // Parse refreshed_at as timestamp
        std::tm tm_buf{};
        std::istringstream iss(meta->refreshed_at);
        iss >> std::get_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
        if (iss.fail()) continue;
        auto refreshed = std::chrono::system_clock::from_time_t(std::mktime(&tm_buf));
        auto age_hours = std::chrono::duration_cast<std::chrono::hours>(now - refreshed).count();

        if (age_hours > ttl_hours) {
            // Calculate size before deletion
            for (const auto& f : std::filesystem::recursive_directory_iterator(entry.path())) {
                if (f.is_regular_file()) space_reclaimed += f.file_size();
            }
            deleted_paths.push_back(entry.path());
            deleted_count++;
            if (!dry_run) {
                std::filesystem::remove_all(entry.path());
            }
        }
    }

    return {deleted_count, deleted_paths, space_reclaimed};
}

// ---------------------------------------------------------------------------
// detect_adr — scan notes.md for Decision: markers
// ---------------------------------------------------------------------------

/** Generate a suggested ADR title in kebab-case from decision text. */
std::string generate_adr_title(const std::string& decision_text) {
    // Take first 50 chars or up to first period
    std::string text = decision_text;
    if (text.length() > 50) text = text.substr(0, 50);
    auto period_pos = text.find('.');
    if (period_pos != std::string::npos) text = text.substr(0, period_pos);

    // Convert non-alphanumeric to hyphens, lowercase
    std::string title;
    title.reserve(text.size());
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            title += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (title.empty() || title.back() != '-') {
            title += '-';
        }
    }
    // Trim trailing hyphens
    while (!title.empty() && title.back() == '-') title.pop_back();

    if (title.empty()) title = "untitled-decision";
    if (title.length() > 40) title = title.substr(0, 40);

    return title;
}

WorksetOps::DetectAdrResult WorksetOps::detect_adr(
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    auto ws_path = resolve_workset_path(item_ref, backlog_root);

    // If not found by direct ref, try to look up the workset path
    if (!ws_path) {
        // resolve_workset_path already searched; nothing more to try
        return {{}, {}};
    }

    auto notes_path = *ws_path / "notes.md";
    if (!std::filesystem::exists(notes_path)) {
        return {*ws_path, {}};
    }

    std::ifstream ifs(notes_path);
    if (!ifs.is_open()) {
        return {*ws_path, {}};
    }

    std::string line;
    std::vector<DetectAdrCandidate> candidates;
    std::regex decision_re(R"(^\s*decision:\s*(.+)$)", std::regex::icase);

    while (std::getline(ifs, line)) {
        std::smatch match;
        if (std::regex_match(line, match, decision_re)) {
            std::string text = trim(match[1].str());
            if (!text.empty()) {
                DetectAdrCandidate cand;
                cand.text = text;
                cand.suggested_title = generate_adr_title(text);
                candidates.push_back(cand);
            }
        }
    }

    return {*ws_path, candidates};
}

} // namespace kano::backlog_ops
