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
    m.seed_items = json_array_to_strings(root["seed_items"]);
    m.pinned_docs = json_array_to_strings(root["pinned_docs"]);

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

// ---------------------------------------------------------------------------
// Item resolution helpers (ported from workset.py _resolve_item_ref)
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

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    ofs << Json::writeString(builder, root) << "\n";
}

TopicOps::CreateResult TopicOps::create_topic(
    const std::string& name,
    const std::string& agent,
    const std::filesystem::path& backlog_root
) {
    auto topic_path = get_topic_path(name, backlog_root);

    if (std::filesystem::exists(topic_path)) {
        throw std::runtime_error("Topic already exists: " + name);
    }

    std::filesystem::create_directories(topic_path);
    std::filesystem::create_directories(topic_path / "materials" / "clips");
    std::filesystem::create_directories(topic_path / "materials" / "links");
    std::filesystem::create_directories(topic_path / "materials" / "extracts");
    std::filesystem::create_directories(topic_path / "materials" / "logs");
    std::filesystem::create_directories(topic_path / "synthesis");
    std::filesystem::create_directories(topic_path / "publish");

    TopicManifest manifest;
    manifest.topic = name;
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
            brief << "# Topic Brief: " << name << "\n\n";
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

    return {name, topic_path, agent, manifest.created_at};
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
