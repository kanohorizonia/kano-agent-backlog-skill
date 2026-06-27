#include "kano/backlog_ops/topic/topic_ops.hpp"
#include "kano/backlog_core/models/models.hpp"
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <map>
#include <utility>
#include <chrono>
#include <ctime>
#include <set>

namespace kano::backlog_ops {
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

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> json_array_to_strings(const Json::Value& value) {
    std::vector<std::string> result;
    if (!value.isArray()) {
        return result;
    }
    for (const auto& entry : value) {
        if (entry.isString()) {
            result.push_back(entry.asString());
        }
    }
    return result;
}

std::vector<int> json_array_to_ints(const Json::Value& value) {
    std::vector<int> result;
    if (!value.isArray()) {
        return result;
    }
    for (const auto& entry : value) {
        if (entry.isInt()) {
            result.push_back(entry.asInt());
        }
    }
    return result;
}

std::optional<std::string> json_optional_string(const Json::Value& value) {
    if (value.isString()) {
        return value.asString();
    }
    return std::nullopt;
}

TopicOps::TopicManifest::SnippetRef parse_snippet_ref(const Json::Value& value) {
    TopicOps::TopicManifest::SnippetRef snippet;
    if (!value.isObject()) {
        return snippet;
    }
    snippet.type = value.get("type", "snippet").asString();
    snippet.repo = value.get("repo", "local").asString();
    snippet.revision = json_optional_string(value["revision"]);
    snippet.file = value.get("file", "").asString();
    snippet.lines = json_array_to_ints(value["lines"]);
    snippet.hash = value.get("hash", "").asString();
    snippet.cached_text = json_optional_string(value["cached_text"]);
    snippet.collected_at = json_optional_string(value["collected_at"]);
    snippet.collector = json_optional_string(value["collector"]);
    return snippet;
}

std::vector<TopicOps::TopicManifest::SnippetRef> json_array_to_snippets(const Json::Value& value) {
    std::vector<TopicOps::TopicManifest::SnippetRef> result;
    if (!value.isArray()) {
        return result;
    }
    for (const auto& entry : value) {
        if (entry.isObject()) {
            result.push_back(parse_snippet_ref(entry));
        }
    }
    return result;
}

Json::Value snippet_ref_to_json(const TopicOps::TopicManifest::SnippetRef& snippet) {
    Json::Value value(Json::objectValue);
    value["type"] = snippet.type.empty() ? "snippet" : snippet.type;
    value["repo"] = snippet.repo.empty() ? "local" : snippet.repo;
    value["revision"] = snippet.revision ? Json::Value(*snippet.revision) : Json::Value(Json::nullValue);
    value["file"] = snippet.file;
    Json::Value lines(Json::arrayValue);
    for (const auto line : snippet.lines) {
        lines.append(line);
    }
    value["lines"] = lines;
    value["hash"] = snippet.hash;
    value["cached_text"] = snippet.cached_text ? Json::Value(*snippet.cached_text) : Json::Value(Json::nullValue);
    value["collected_at"] = snippet.collected_at ? Json::Value(*snippet.collected_at) : Json::Value(Json::nullValue);
    value["collector"] = snippet.collector ? Json::Value(*snippet.collector) : Json::Value(Json::nullValue);
    return value;
}

std::optional<TopicOps::TopicManifest> parse_manifest(const std::string& content) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::istringstream input(content);
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors) || !root.isObject()) {
        return std::nullopt;
    }

    TopicOps::TopicManifest m;
    m.topic = root.get("topic", "").asString();
    m.agent = root.get("agent", "unknown").asString();
    m.created_at = root.get("created_at", "").asString();
    m.updated_at = root.get("updated_at", "").asString();
    m.status = root.get("status", "open").asString();
    m.closed_at = json_optional_string(root["closed_at"]);
    m.seed_items = json_array_to_strings(root["seed_items"]);
    m.pinned_docs = json_array_to_strings(root["pinned_docs"]);
    m.snippet_refs = json_array_to_snippets(root["snippet_refs"]);
    m.related_topics = json_array_to_strings(root["related_topics"]);
    m.has_spec = root.get("has_spec", false).asBool();

    if (m.topic.empty()) return std::nullopt;
    return m;
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

std::optional<int> parse_iso_date_days(const std::string& value) {
    if (value.size() < 10 || value[4] != '-' || value[7] != '-') {
        return std::nullopt;
    }
    try {
        const int year = std::stoi(value.substr(0, 4));
        const int month = std::stoi(value.substr(5, 2));
        const int day = std::stoi(value.substr(8, 2));
        if (month < 1 || month > 12 || day < 1 || day > 31) {
            return std::nullopt;
        }
        const int adjusted_year = month <= 2 ? year - 1 : year;
        const int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(adjusted_year - era * 400);
        const unsigned doy = (153 * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int>(doe) - 719468;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int days_between(const std::string& as_of, const std::string& timestamp) {
    const auto as_of_days = parse_iso_date_days(as_of);
    const auto timestamp_days = parse_iso_date_days(timestamp);
    if (!as_of_days || !timestamp_days) {
        return -1;
    }
    return std::max(0, *as_of_days - *timestamp_days);
}

std::uintmax_t directory_size_bytes(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) {
        return 0;
    }
    std::uintmax_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end;
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        total += it->file_size(ec);
        if (ec) {
            ec.clear();
        }
    }
    return total;
}

int count_regular_files_recursive(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) {
        return 0;
    }
    int count = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end;
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_regular_file(ec)) {
            ++count;
        }
        if (ec) {
            ec.clear();
        }
    }
    return count;
}

std::string lower_local(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool is_done_state(const std::string& state) {
    const auto normalized = lower_local(state);
    return normalized == "done" || normalized == "duplicate";
}

bool is_dropped_state(const std::string& state) {
    return lower_local(state) == "dropped";
}

std::optional<Json::Value> read_json_object_if_exists(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors) || !root.isObject()) {
        return std::nullopt;
    }
    return root;
}

std::map<std::string, std::string> topic_state_names_by_id(const std::filesystem::path& backlog_root) {
    std::map<std::string, std::string> names;
    const auto topics_state_root = backlog_root / ".cache" / "worksets" / "topics";
    if (!std::filesystem::exists(topics_state_root)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(topics_state_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        auto doc = read_json_object_if_exists(entry.path());
        if (!doc) {
            continue;
        }
        const auto id = doc->get("topic_id", "").asString();
        const auto name = doc->get("name", "").asString();
        if (!id.empty() && !name.empty()) {
            names[id] = name;
        }
    }
    return names;
}

std::map<std::string, std::vector<std::string>> active_agents_by_topic(const std::filesystem::path& backlog_root) {
    std::map<std::string, std::set<std::string>> grouped;
    const auto names_by_id = topic_state_names_by_id(backlog_root);
    const auto state = read_json_object_if_exists(backlog_root / ".cache" / "worksets" / "state.json");
    if (state) {
        const auto& agents = (*state)["agents"];
        if (agents.isObject()) {
            for (const auto& agent : agents.getMemberNames()) {
                const auto topic_id = agents[agent].get("active_topic_id", "").asString();
                const auto found = names_by_id.find(topic_id);
                if (found != names_by_id.end()) {
                    grouped[found->second].insert(agent);
                }
            }
        }
    }
    const auto legacy_root = backlog_root / ".cache" / "worksets";
    if (std::filesystem::exists(legacy_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(legacy_root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto filename = entry.path().filename().string();
            constexpr std::string_view prefix = "active_topic.";
            constexpr std::string_view suffix = ".txt";
            if (!filename.starts_with(prefix) || !filename.ends_with(suffix)) {
                continue;
            }
            std::ifstream input(entry.path(), std::ios::binary);
            std::string topic;
            std::getline(input, topic);
            topic = trim(topic);
            if (!topic.empty()) {
                const auto agent = filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
                grouped[topic].insert(agent);
            }
        }
    }
    const auto topics_root = backlog_root / "topics";
    if (std::filesystem::exists(topics_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(topics_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto marker = entry.path() / ".active";
            if (!std::filesystem::exists(marker)) {
                continue;
            }
            std::ifstream input(marker, std::ios::binary);
            std::string topic;
            std::getline(input, topic);
            topic = trim(topic);
            if (!topic.empty()) {
                grouped[topic].insert("legacy-active-marker");
            }
        }
    }
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& [topic, agents] : grouped) {
        result[topic] = std::vector<std::string>(agents.begin(), agents.end());
    }
    return result;
}

Json::Value string_vector_to_json(const std::vector<std::string>& values) {
    Json::Value result(Json::arrayValue);
    for (const auto& value : values) {
        result.append(value);
    }
    return result;
}

std::string recommendation_for(const TopicOps::TopicAuditEntry& entry, int ttl_days, int stale_days, int days_since_close) {
    if (!entry.active_agents.empty()) {
        return "keep";
    }
    if (entry.status == "closed") {
        if (!entry.closed_at.empty() && days_since_close >= ttl_days) {
            return entry.materials_present ? "cleanup_materials_candidate" : "delete_topic_candidate";
        }
        return "keep";
    }
    if (entry.inactive_days >= stale_days) {
        if (entry.open_item_count == 0) {
            return "close_candidate";
        }
        if (entry.materials_present || entry.snapshot_count > 0 || entry.pinned_doc_count > 0) {
            return "distill";
        }
        return "manual_review";
    }
    return "keep";
}

// ---------------------------------------------------------------------------
// Item resolution helpers shared by topic and workset-style commands.
// ---------------------------------------------------------------------------

/**
 * Parse YAML frontmatter from an item markdown file.
 * Returns a map of frontmatter key-value pairs.
 */
std::optional<std::map<std::string, std::string>> parse_frontmatter(
    const std::filesystem::path& item_path
) {
    if (!std::filesystem::exists(item_path)) return std::nullopt;

    std::ifstream ifs(item_path);
    if (!ifs.is_open()) return std::nullopt;

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) lines.push_back(line);
    if (lines.empty() || trim(lines[0]) != "---") return std::nullopt;

    size_t end_idx = size_t(-1);
    for (size_t i = 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "---") { end_idx = i; break; }
    }
    if (end_idx == size_t(-1)) return std::nullopt;

    auto result = std::make_optional<std::map<std::string, std::string>>();
    for (size_t i = 1; i < end_idx; ++i) {
        auto colon_pos = lines[i].find(':');
        if (colon_pos == std::string::npos) continue;
        std::string key = trim(lines[i].substr(0, colon_pos));
        std::string val = trim(lines[i].substr(colon_pos + 1));
        // Strip surrounding quotes
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        (*result)[key] = val;
    }
    return result;
}

/**
 * Resolve an item_ref (ID, UID, or path) to its file path and metadata.
 * Searches products/STAR/items/ and legacy items/ layout (STAR = any subdir).
 */
std::optional<std::pair<std::filesystem::path, std::map<std::string, std::string>>>
resolve_item_ref(
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    // Case 1: looks like a path
    if (item_ref.find('/') != std::string::npos ||
        item_ref.find('\\') != std::string::npos ||
        item_ref.find(".md") != std::string::npos) {
        std::filesystem::path p(item_ref);
        if (!p.is_absolute()) p = backlog_root / p;
        p = p.lexically_normal();
        if (std::filesystem::exists(p)) {
            auto fm = parse_frontmatter(p);
            if (fm) return std::make_optional(std::make_pair(p, *fm));
        }
    }

    // Case 2: search by ID/UID in products/*/items/
    auto products_dir = backlog_root / "products";
    if (std::filesystem::exists(products_dir)) {
        for (auto it = std::filesystem::directory_iterator(products_dir);
             it != std::filesystem::directory_iterator(); ++it) {
            if (!it->is_directory()) continue;
            auto items_root = it->path() / "items";
            if (!std::filesystem::exists(items_root)) continue;

            // Quick filename match: {item_ref}_*.md
            for (auto pit = std::filesystem::directory_iterator(items_root);
                 pit != std::filesystem::directory_iterator(); ++pit) {
                if (!pit->is_regular_file()) continue;
                auto fname = pit->path().filename().string();
                if (fname.find(".index.md") != std::string::npos) continue;
                auto basename = pit->path().stem().string();
                // Match: item_ref as prefix or exact ID
                if (basename == item_ref || basename.find(item_ref + "_") == 0) {
                    auto fm = parse_frontmatter(pit->path());
                    if (fm) return std::make_optional(std::make_pair(pit->path(), *fm));
                }
            }

            // Fallback: scan all and check id/uid fields
            for (auto pit = std::filesystem::recursive_directory_iterator(items_root);
                 pit != std::filesystem::recursive_directory_iterator(); ++pit) {
                if (!pit->is_regular_file()) continue;
                if (pit->path().filename().string().find(".index.md") != std::string::npos) continue;
                auto fm = parse_frontmatter(pit->path());
                if (fm) {
                    auto id_it = fm->find("id");
                    auto uid_it = fm->find("uid");
                    if ((id_it != fm->end() && id_it->second == item_ref) ||
                        (uid_it != fm->end() && uid_it->second == item_ref)) {
                        return std::make_optional(std::make_pair(pit->path(), *fm));
                    }
                }
            }
        }
    }

    // Case 3: legacy layout: items/ directly under backlog_root
    auto legacy_root = backlog_root / "items";
    if (std::filesystem::exists(legacy_root)) {
        for (auto pit = std::filesystem::directory_iterator(legacy_root);
             pit != std::filesystem::directory_iterator(); ++pit) {
            if (!pit->is_regular_file()) continue;
            auto fname = pit->path().filename().string();
            if (fname.find(".index.md") != std::string::npos) continue;
            auto basename = pit->path().stem().string();
            if (basename == item_ref || basename.find(item_ref + "_") == 0) {
                auto fm = parse_frontmatter(pit->path());
                if (fm) return std::make_optional(std::make_pair(pit->path(), *fm));
            }
        }
    }

    return std::nullopt;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// TopicOps implementation
// ---------------------------------------------------------------------------

std::filesystem::path TopicOps::get_topic_path(
    const std::string& topic_name,
    const std::filesystem::path& backlog_root
) {
    return backlog_root / "topics" / topic_name;
}

bool TopicOps::has_date_prefix(const std::string& topic_name) {
    static const std::regex date_prefix_re(R"(^\d{4}-\d{2}-\d{2}-[A-Za-z0-9][A-Za-z0-9_-]*$)");
    return std::regex_match(topic_name, date_prefix_re);
}

void TopicOps::validate_topic_name(const std::string& topic_name) {
    const auto name = trim(topic_name);
    if (name.empty()) {
        throw std::runtime_error("Topic name cannot be empty");
    }
    if (name == "." || name == "..") {
        throw std::runtime_error("Topic name cannot be '.' or '..'");
    }
    if (name.size() > 64) {
        throw std::runtime_error("Topic name too long (" + std::to_string(name.size()) + " chars, max 64)");
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        throw std::runtime_error("Topic name must not contain path separators");
    }
    if (name.find("..") != std::string::npos) {
        throw std::runtime_error("Topic name must not contain traversal segments");
    }
    static const std::regex legacy_name_re(R"(^[A-Za-z][A-Za-z0-9_-]*$)");
    if (!std::regex_match(name, legacy_name_re) && !has_date_prefix(name)) {
        throw std::runtime_error("Topic name must be a legacy letter-start slug or YYYY-MM-DD-prefixed slug with only alphanumeric characters, hyphens, and underscores");
    }
    static const std::set<std::string> reserved = {"items", "topics", "cache", "index", "meta"};
    if (reserved.count(lower_local(name)) > 0) {
        throw std::runtime_error("Topic name '" + name + "' is reserved");
    }
}

std::optional<TopicOps::TopicManifest> TopicOps::load_manifest(
    const std::filesystem::path& topic_path
) {
    auto manifest_path = topic_path / "manifest.json";
    if (!std::filesystem::exists(manifest_path)) return std::nullopt;

    std::ifstream ifs(manifest_path);
    if (!ifs.is_open()) return std::nullopt;

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return parse_manifest(buffer.str());
}

void TopicOps::save_manifest(
    const std::filesystem::path& topic_path,
    const TopicManifest& manifest
) {
    auto manifest_path = topic_path / "manifest.json";
    std::ofstream ofs(manifest_path);
    if (!ofs.is_open()) return;

    Json::Value root(Json::objectValue);
    root["topic"] = manifest.topic;
    root["agent"] = manifest.agent;
    root["created_at"] = manifest.created_at;
    root["updated_at"] = manifest.updated_at;
    root["status"] = manifest.status;
    root["closed_at"] = manifest.closed_at ? Json::Value(*manifest.closed_at) : Json::Value(Json::nullValue);

    Json::Value seed_items(Json::arrayValue);
    for (const auto& item : manifest.seed_items) {
        seed_items.append(item);
    }
    root["seed_items"] = seed_items;

    Json::Value pinned_docs(Json::arrayValue);
    for (const auto& doc : manifest.pinned_docs) {
        pinned_docs.append(doc);
    }
    root["pinned_docs"] = pinned_docs;

    Json::Value snippet_refs(Json::arrayValue);
    for (const auto& snippet : manifest.snippet_refs) {
        snippet_refs.append(snippet_ref_to_json(snippet));
    }
    root["snippet_refs"] = snippet_refs;

    Json::Value related_topics(Json::arrayValue);
    for (const auto& topic : manifest.related_topics) {
        related_topics.append(topic);
    }
    root["related_topics"] = related_topics;
    root["has_spec"] = manifest.has_spec;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    ofs << Json::writeString(builder, root) << "\n";
}

TopicOps::CreateResult TopicOps::create_topic(
    const std::string& name,
    const std::string& agent,
    const std::filesystem::path& backlog_root
) {
    const auto topic_name = trim(name);
    validate_topic_name(topic_name);
    auto topic_path = get_topic_path(topic_name, backlog_root);

    if (std::filesystem::exists(topic_path)) {
        throw std::runtime_error("Topic already exists: " + topic_name);
    }

    std::filesystem::create_directories(topic_path);
    std::filesystem::create_directories(topic_path / "materials" / "clips");
    std::filesystem::create_directories(topic_path / "materials" / "links");
    std::filesystem::create_directories(topic_path / "materials" / "extracts");
    std::filesystem::create_directories(topic_path / "materials" / "logs");
    std::filesystem::create_directories(topic_path / "synthesis");
    std::filesystem::create_directories(topic_path / "publish");

    TopicManifest manifest;
    manifest.topic = topic_name;
    manifest.agent = agent;
    manifest.created_at = current_iso_timestamp();
    manifest.updated_at = manifest.created_at;
    manifest.status = "open";
    manifest.seed_items = {};
    manifest.pinned_docs = {};

    save_manifest(topic_path, manifest);

    {
        std::ofstream brief(topic_path / "brief.md");
        if (brief.is_open()) {
            brief << "# Topic Brief: " << topic_name << "\n\n";
            brief << "Human-maintained notes for this topic.\n";
        }
    }
    {
        std::ofstream notes(topic_path / "notes.md");
        if (notes.is_open()) {
            notes << "# Notes\n\n";
            notes << "Decision to make:\n\n";
            notes << "Options:\n\n";
            notes << "Evidence:\n\n";
            notes << "Recommendation:\n";
        }
    }

    return {topic_name, topic_path, agent, manifest.created_at};
}

TopicOps::AddItemResult TopicOps::add_item(
    const std::string& topic_name,
    const std::string& item_ref,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(topic_name, backlog_root);
    if (!std::filesystem::exists(topic_path)) {
        throw std::runtime_error("Topic not found: " + topic_name);
    }

    auto manifest = load_manifest(topic_path);
    if (!manifest) {
        throw std::runtime_error("Failed to load manifest for topic: " + topic_name);
    }

    bool added = false;
    auto& items = manifest->seed_items;
    auto resolved = resolve_item_ref(item_ref, backlog_root);
    if (!resolved) {
        throw std::runtime_error("Item not found: " + item_ref);
    }

    const auto& resolved_pair = *resolved;
    const auto& metadata = resolved_pair.second;
    auto uid_it = metadata.find("uid");
    auto id_it = metadata.find("id");
    std::string manifest_ref = uid_it != metadata.end() && !uid_it->second.empty()
        ? uid_it->second
        : (id_it != metadata.end() ? id_it->second : item_ref);

    if (std::find(items.begin(), items.end(), manifest_ref) == items.end()) {
        items.push_back(manifest_ref);
        added = true;
    }

    manifest->updated_at = current_iso_timestamp();
    save_manifest(topic_path, *manifest);

    return {topic_name, manifest_ref, added};
}

std::vector<TopicOps::TopicStatus> TopicOps::list_topics(
    const std::filesystem::path& backlog_root,
    const std::optional<std::string>& active_agent
) {
    std::vector<TopicStatus> result;
    auto topics_root = backlog_root / "topics";

    if (!std::filesystem::exists(topics_root)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(topics_root)) {
        if (!entry.is_directory()) continue;
        auto topic_path = entry.path();
        auto manifest = load_manifest(topic_path);
        if (!manifest) continue;

        TopicStatus status;
        status.id = manifest->topic;
        status.title = manifest->topic;
        status.status = manifest->status;
        status.is_active = false; // determined below if active_agent set
        status.item_count = static_cast<int>(manifest->seed_items.size());
        status.pinned_doc_count = static_cast<int>(manifest->pinned_docs.size());
        status.created_at = manifest->created_at;
        status.updated_at = manifest->updated_at;

        if (active_agent) {
            auto active_path = topic_path / ".active";
            if (std::filesystem::exists(active_path)) {
                std::ifstream ifs(active_path);
                std::string active_topic;
                ifs >> active_topic;
                status.is_active = (active_topic == manifest->topic);
            }
        }

        result.push_back(status);
    }

    std::sort(result.begin(), result.end(),
        [](const TopicStatus& a, const TopicStatus& b) { return a.id < b.id; });

    return result;
}

TopicOps::TopicAuditReport TopicOps::audit_topics(
    const std::filesystem::path& backlog_root
) {
    return audit_topics(backlog_root, TopicAuditOptions{});
}

TopicOps::TopicAuditReport TopicOps::audit_topics(
    const std::filesystem::path& backlog_root,
    const TopicAuditOptions& options
) {
    if (options.ttl_days <= 0) {
        throw std::runtime_error("ttl-days must be > 0");
    }
    if (options.stale_days <= 0) {
        throw std::runtime_error("stale-days must be > 0");
    }

    TopicAuditReport report;
    report.ttl_days = options.ttl_days;
    report.stale_days = options.stale_days;
    report.as_of = options.as_of.value_or(current_iso_timestamp()).substr(0, 10);
    report.mutated = false;

    const auto topics_root = backlog_root / "topics";
    if (!std::filesystem::exists(topics_root)) {
        return report;
    }

    const auto active_by_topic = active_agents_by_topic(backlog_root);
    for (const auto& entry : std::filesystem::directory_iterator(topics_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto topic_path = entry.path();
        const auto topic_dir_name = topic_path.filename().string();
        const auto manifest = load_manifest(topic_path);
        if (!manifest) {
            TopicAuditEntry audit;
            audit.topic = topic_dir_name;
            audit.path = topic_path.string();
            audit.status = "invalid";
            audit.has_date_prefix = has_date_prefix(audit.topic);
            if (audit.has_date_prefix) {
                audit.date_prefix = audit.topic.substr(0, 10);
            } else {
                audit.stale_reasons.push_back("missing_date_prefix");
            }
            const auto active_it = active_by_topic.find(audit.topic);
            if (active_it != active_by_topic.end()) {
                audit.active_agents = active_it->second;
                audit.stale_reasons.push_back("active_topic");
            }
            audit.stale_reasons.push_back("invalid_manifest");
            audit.recommendation = "manual_review";
            report.topics.push_back(audit);
            continue;
        }

        TopicAuditEntry audit;
        audit.topic = manifest->topic;
        audit.path = topic_path.string();
        audit.status = manifest->status.empty() ? "open" : manifest->status;
        audit.created_at = manifest->created_at;
        audit.updated_at = manifest->updated_at;
        audit.closed_at = manifest->closed_at.value_or("");
        audit.age_days = days_between(report.as_of, audit.created_at);
        audit.inactive_days = days_between(report.as_of, audit.updated_at.empty() ? audit.created_at : audit.updated_at);
        audit.item_count = static_cast<int>(manifest->seed_items.size());
        audit.pinned_doc_count = static_cast<int>(manifest->pinned_docs.size());
        audit.has_date_prefix = has_date_prefix(audit.topic);
        if (audit.has_date_prefix) {
            audit.date_prefix = audit.topic.substr(0, 10);
        }

        const auto active_it = active_by_topic.find(audit.topic);
        if (active_it != active_by_topic.end()) {
            audit.active_agents = active_it->second;
        }

        for (const auto& item : manifest->seed_items) {
            const auto resolved = resolve_item_ref(item, backlog_root);
            if (!resolved) {
                ++audit.open_item_count;
                audit.stale_reasons.push_back("unresolved_item:" + item);
                continue;
            }
            const auto state_it = resolved->second.find("state");
            const auto state = state_it == resolved->second.end() ? std::string() : state_it->second;
            if (is_done_state(state)) {
                ++audit.done_item_count;
            } else if (is_dropped_state(state)) {
                ++audit.dropped_item_count;
            } else {
                ++audit.open_item_count;
            }
        }

        const auto materials_path = topic_path / "materials";
        audit.materials_size_bytes = directory_size_bytes(materials_path);
        audit.materials_present = std::filesystem::exists(materials_path) && audit.materials_size_bytes > 0;
        audit.snapshot_count = count_regular_files_recursive(topic_path / "snapshots");

        const int days_since_close = audit.closed_at.empty() ? -1 : days_between(report.as_of, audit.closed_at);
        if (!audit.has_date_prefix) {
            audit.stale_reasons.push_back("missing_date_prefix");
        }
        if (!audit.active_agents.empty()) {
            audit.stale_reasons.push_back("active_topic");
        }
        if (audit.status == "closed" && days_since_close >= options.ttl_days) {
            audit.stale_reasons.push_back(audit.materials_present ? "closed_materials_ttl_expired" : "closed_topic_ttl_expired");
        } else if (audit.status != "closed" && audit.inactive_days >= options.stale_days) {
            audit.stale_reasons.push_back("open_topic_stale");
            if (audit.open_item_count == 0) {
                audit.stale_reasons.push_back("no_open_items");
            }
        }
        audit.recommendation = recommendation_for(audit, options.ttl_days, options.stale_days, days_since_close);
        report.topics.push_back(audit);
    }

    std::sort(report.topics.begin(), report.topics.end(), [](const TopicAuditEntry& lhs, const TopicAuditEntry& rhs) {
        return lhs.topic < rhs.topic;
    });
    return report;
}

std::string TopicOps::render_audit_report(
    const TopicAuditReport& report,
    const std::string& format
) {
    const auto normalized_format = lower_local(format.empty() ? std::string("plain") : format);
    if (normalized_format != "plain" && normalized_format != "json" && normalized_format != "markdown") {
        throw std::runtime_error("format must be one of: plain, json, markdown");
    }

    if (normalized_format == "json") {
        Json::Value root(Json::objectValue);
        root["as_of"] = report.as_of;
        root["ttl_days"] = report.ttl_days;
        root["stale_days"] = report.stale_days;
        root["mutated"] = report.mutated;
        Json::Value topics(Json::arrayValue);
        for (const auto& entry : report.topics) {
            Json::Value item(Json::objectValue);
            item["topic"] = entry.topic;
            item["path"] = entry.path;
            item["status"] = entry.status;
            item["created_at"] = entry.created_at;
            item["updated_at"] = entry.updated_at;
            item["closed_at"] = entry.closed_at.empty() ? Json::Value(Json::nullValue) : Json::Value(entry.closed_at);
            item["age_days"] = entry.age_days;
            item["inactive_days"] = entry.inactive_days;
            item["active_agents"] = string_vector_to_json(entry.active_agents);
            item["item_count"] = entry.item_count;
            item["open_item_count"] = entry.open_item_count;
            item["done_item_count"] = entry.done_item_count;
            item["dropped_item_count"] = entry.dropped_item_count;
            item["pinned_doc_count"] = entry.pinned_doc_count;
            item["materials_present"] = entry.materials_present;
            item["materials_size_bytes"] = Json::UInt64(entry.materials_size_bytes);
            item["snapshot_count"] = entry.snapshot_count;
            item["has_date_prefix"] = entry.has_date_prefix;
            item["date_prefix"] = entry.date_prefix.empty() ? Json::Value(Json::nullValue) : Json::Value(entry.date_prefix);
            item["stale_reasons"] = string_vector_to_json(entry.stale_reasons);
            item["recommendation"] = entry.recommendation;
            topics.append(item);
        }
        root["topics"] = topics;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        return Json::writeString(builder, root) + "\n";
    }

    std::ostringstream out;
    if (normalized_format == "markdown") {
        out << "# Topic Audit\n\n";
        out << "- As of: " << report.as_of << "\n";
        out << "- TTL days: " << report.ttl_days << "\n";
        out << "- Stale days: " << report.stale_days << "\n";
        out << "- Mutated: " << (report.mutated ? "true" : "false") << "\n\n";
        out << "| Topic | Status | Age | Inactive | Items | Open | Date prefix | Recommendation |\n";
        out << "| --- | --- | ---: | ---: | ---: | ---: | --- | --- |\n";
        for (const auto& entry : report.topics) {
            out << "| " << entry.topic
                << " | " << entry.status
                << " | " << entry.age_days
                << " | " << entry.inactive_days
                << " | " << entry.item_count
                << " | " << entry.open_item_count
                << " | " << (entry.has_date_prefix ? "yes" : "no")
                << " | " << entry.recommendation
                << " |\n";
        }
        return out.str();
    }

    out << "Topic audit as of " << report.as_of
        << " (ttl_days=" << report.ttl_days
        << ", stale_days=" << report.stale_days
        << ", mutated=" << (report.mutated ? "true" : "false") << ")\n";
    if (report.topics.empty()) {
        out << "No topics found.\n";
        return out.str();
    }
    for (const auto& entry : report.topics) {
        out << entry.topic << " [" << entry.status << "] recommendation=" << entry.recommendation << "\n";
        out << "  Path: " << entry.path << "\n";
        out << "  Created: " << entry.created_at << " Updated: " << entry.updated_at;
        if (!entry.closed_at.empty()) {
            out << " Closed: " << entry.closed_at;
        }
        out << "\n";
        out << "  Age days: " << entry.age_days << " Inactive days: " << entry.inactive_days << "\n";
        out << "  Items: total=" << entry.item_count
            << " open=" << entry.open_item_count
            << " done=" << entry.done_item_count
            << " dropped=" << entry.dropped_item_count << "\n";
        out << "  Pinned docs: " << entry.pinned_doc_count
            << " Materials: " << (entry.materials_present ? "yes" : "no")
            << " (" << entry.materials_size_bytes << " bytes)"
            << " Snapshots: " << entry.snapshot_count << "\n";
        out << "  Date prefix: " << (entry.has_date_prefix ? entry.date_prefix : "missing") << "\n";
        if (!entry.active_agents.empty()) {
            out << "  Active agents: ";
            for (std::size_t index = 0; index < entry.active_agents.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << entry.active_agents[index];
            }
            out << "\n";
        }
        if (!entry.stale_reasons.empty()) {
            out << "  Reasons: ";
            for (std::size_t index = 0; index < entry.stale_reasons.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << entry.stale_reasons[index];
            }
            out << "\n";
        }
    }
    return out.str();
}

TopicOps::SwitchResult TopicOps::switch_topic(
    const std::string& topic_name,
    const std::string& agent,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(topic_name, backlog_root);
    if (!std::filesystem::exists(topic_path)) {
        throw std::runtime_error("Topic not found: " + topic_name);
    }

    auto manifest = load_manifest(topic_path);
    if (!manifest) {
        throw std::runtime_error("Failed to load manifest for topic: " + topic_name);
    }

    // Write active marker for this agent
    auto active_path = topic_path / ".active";
    std::ofstream ofs(active_path);
    ofs << manifest->topic;

    return {topic_name,
            static_cast<int>(manifest->seed_items.size()),
            static_cast<int>(manifest->pinned_docs.size())};
}

std::filesystem::path TopicOps::distill(
    const std::string& topic_name,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(topic_name, backlog_root);
    if (!std::filesystem::exists(topic_path)) {
        throw std::runtime_error("Topic not found: " + topic_name);
    }

    auto brief_path = topic_path / "brief.generated.md";
    auto manifest = load_manifest(topic_path);
    if (!manifest) return brief_path;

    std::ofstream ofs(brief_path);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to write brief.generated.md for topic: " + topic_name);
    }

    ofs << "# Topic: " << manifest->topic << "\n\n";
    ofs << "**Generated**: " << current_iso_timestamp() << "\n";
    ofs << "**Agent**: " << manifest->agent << "\n\n";

    ofs << "## Related Topics\n\n";
    if (manifest->related_topics.empty()) {
        ofs << "- (none)\n\n";
    } else {
        for (const auto& topic : manifest->related_topics) {
            ofs << "- " << topic << "\n";
        }
        ofs << "\n";
    }

    if (!manifest->seed_items.empty()) {
        ofs << "## Items (" << manifest->seed_items.size() << ")\n\n";
        for (const auto& item : manifest->seed_items) {
            ofs << "- " << item << "\n";
        }
        ofs << "\n";
    }

    if (!manifest->pinned_docs.empty()) {
        ofs << "## Pinned Documents (" << manifest->pinned_docs.size() << ")\n\n";
        for (const auto& doc : manifest->pinned_docs) {
            ofs << "- " << doc << "\n";
        }
        ofs << "\n";
    }

    auto spec_dir = topic_path / "spec";
    if (std::filesystem::exists(spec_dir)) {
        std::vector<std::pair<std::filesystem::path, std::string>> spec_files = {
            {spec_dir / "requirements.md", "Requirements"},
            {spec_dir / "design.md", "Design"},
            {spec_dir / "tasks.md", "Tasks"}
        };
        bool wrote_spec_heading = false;
        for (const auto& [path, label] : spec_files) {
            if (!std::filesystem::exists(path)) {
                continue;
            }
            if (!wrote_spec_heading) {
                ofs << "## Specification\n\n";
                wrote_spec_heading = true;
            }
            ofs << "- [" << label << "](spec/" << path.filename().string() << ")\n";
        }
        if (wrote_spec_heading) {
            ofs << "\n";
        }
    }

    ofs << "## Snippet Refs\n\n";
    if (manifest->snippet_refs.empty()) {
        ofs << "- (none)\n";
    } else {
        for (const auto& snippet : manifest->snippet_refs) {
            std::string range;
            if (snippet.lines.size() >= 2) {
                range = "#L" + std::to_string(snippet.lines[0]) + "-L" + std::to_string(snippet.lines[1]);
            }
            ofs << "- " << snippet.file << range;
            if (!snippet.hash.empty()) {
                ofs << " (" << snippet.hash << ")";
            }
            ofs << "\n";
        }
    }

    ofs.flush();
    if (!ofs.good()) {
        throw std::runtime_error("Failed to flush brief.generated.md for topic: " + topic_name);
    }

    if (!std::filesystem::exists(brief_path)) {
        throw std::runtime_error("brief.generated.md was not created for topic: " + topic_name);
    }

    return brief_path;
}

std::optional<std::string> TopicOps::get_current_topic(
    const std::string& agent,
    const std::filesystem::path& backlog_root
) {
    auto topics_root = backlog_root / "topics";
    if (!std::filesystem::exists(topics_root)) return std::nullopt;

    for (const auto& entry : std::filesystem::directory_iterator(topics_root)) {
        if (!entry.is_directory()) continue;
        auto active_path = entry.path() / ".active";
        if (!std::filesystem::exists(active_path)) continue;
        std::ifstream ifs(active_path);
        std::string t;
        ifs >> t;
        return t;
    }
    return std::nullopt;
}

TopicOps::TopicStatus TopicOps::get_topic_status(
    const std::string& topic_name,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(topic_name, backlog_root);
    auto manifest = load_manifest(topic_path);

    TopicStatus status;
    status.id = topic_name;
    status.title = topic_name;
    status.status = "open";
    status.is_active = false;
    status.item_count = 0;
    status.pinned_doc_count = 0;
    status.created_at = "";
    status.updated_at = "";

    if (manifest) {
        status.status = manifest->status;
        status.item_count = static_cast<int>(manifest->seed_items.size());
        status.pinned_doc_count = static_cast<int>(manifest->pinned_docs.size());
        status.created_at = manifest->created_at;
        status.updated_at = manifest->updated_at;

        auto active_path = topic_path / ".active";
        status.is_active = std::filesystem::exists(active_path);
    }

    return status;
}

TopicOps::TopicContextBundle TopicOps::export_context(
    const std::string& topic_name,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(topic_name, backlog_root);
    auto manifest_path = topic_path / "manifest.json";

    if (!std::filesystem::exists(manifest_path)) {
        throw std::runtime_error("Topic not found: " + topic_name);
    }

    auto manifest = load_manifest(topic_path);
    if (!manifest) {
        throw std::runtime_error("Failed to load manifest for topic: " + topic_name);
    }

    TopicContextBundle bundle;
    bundle.topic = topic_name;
    bundle.generated_at = current_iso_timestamp();

    // Resolve workspace root (backlog_root's parent parent: _kano/ → repo root)
    auto workspace_root = backlog_root / ".." / "..";
    workspace_root = workspace_root.lexically_normal();

    // Load item summaries
    for (const auto& item_uid : manifest->seed_items) {
        ItemSummary summary;
        summary.uid = item_uid;

        auto resolved = resolve_item_ref(item_uid, backlog_root);
        if (resolved) {
            const auto& [item_path, fm] = *resolved;
            auto get = [&](const std::string& k) -> std::string {
                auto it = fm.find(k);
                return (it != fm.end()) ? it->second : "";
            };
            summary.id = get("id");
            summary.title = get("title");
            summary.type = get("type");
            summary.state = get("state");
            summary.priority = get("priority");
            summary.path = item_path.lexically_relative(workspace_root).string();
        } else {
            summary.error = true;
            summary.error_msg = "Item not found or could not be loaded";
        }
        bundle.items.push_back(summary);
    }

    // Load pinned document content
    for (const auto& doc_path_str : manifest->pinned_docs) {
        PinnedDocEntry entry;
        entry.path = doc_path_str;

        auto doc_path = workspace_root / doc_path_str;
        if (std::filesystem::exists(doc_path)) {
            std::ifstream ifs(doc_path);
            if (ifs.is_open()) {
                std::stringstream ss;
                ss << ifs.rdbuf();
                entry.content = ss.str();
            } else {
                entry.error = true;
                entry.error_msg = "Could not read file";
            }
        } else {
            entry.error = true;
            entry.error_msg = "Document not found";
        }
        bundle.pinned_docs.push_back(entry);
    }

    return bundle;
}

} // namespace kano::backlog_ops
