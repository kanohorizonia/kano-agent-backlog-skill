#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <algorithm>
#include <fstream>
#include <cctype>
#include <regex>
#include <chrono>
#include <random>
#include <iomanip>

namespace kano::backlog_core {

namespace {

bool is_blank(const std::string& value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char ch) { return std::isspace(ch); });
}

bool is_null_like(const YAML::Node& node) {
    if (!node || node.IsNull()) {
        return true;
    }
    if (!node.IsScalar()) {
        return false;
    }

    std::string value = node.as<std::string>();
    if (value.empty() || is_blank(value)) {
        return true;
    }

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value == "null" || value == "none";
}

std::optional<std::string> parse_optional_string(const YAML::Node& node) {
    if (is_null_like(node)) {
        return std::nullopt;
    }
    return node.as<std::string>();
}

std::vector<std::string> parse_string_list(const YAML::Node& node) {
    std::vector<std::string> values;
    if (!node || node.IsNull() || !node.IsSequence()) {
        return values;
    }
    for (const auto& entry : node) {
        if (!entry.IsNull()) {
            values.push_back(entry.as<std::string>());
        }
    }
    return values;
}

}  // namespace

CanonicalStore::CanonicalStore(const std::filesystem::path& product_root) 
    : product_root_(product_root), items_root_(product_root / "items") {}

BacklogItem CanonicalStore::read(const std::filesystem::path& item_path) const {
    if (!std::filesystem::exists(item_path)) {
        throw ItemNotFoundError(item_path.string());
    }

    std::ifstream f(item_path);
    if (!f.is_open()) {
        throw ParseError(item_path.string(), "Failed to open file");
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();

    auto ctx = Frontmatter::parse(content);
    if (ctx.metadata.IsNull()) {
        throw ParseError(item_path.string(), "Invalid or missing frontmatter");
    }

    try {
        BacklogItem item;
        item.file_path = item_path;
        
        // Map frontmatter
        item.id = ctx.metadata["id"].as<std::string>();
        item.uid = ctx.metadata["uid"].as<std::string>();
        item.type = parse_item_type(ctx.metadata["type"].as<std::string>()).value();
        item.title = ctx.metadata["title"].as<std::string>();
        item.state = parse_item_state(ctx.metadata["state"].as<std::string>()).value();

        item.priority = parse_optional_string(ctx.metadata["priority"]);
        item.parent = parse_optional_string(ctx.metadata["parent"]);
        item.owner = parse_optional_string(ctx.metadata["owner"]);
        item.area = parse_optional_string(ctx.metadata["area"]);
        item.iteration = parse_optional_string(ctx.metadata["iteration"]);
        item.created = ctx.metadata["created"].as<std::string>();
        item.updated = ctx.metadata["updated"].as<std::string>();

        item.tags = parse_string_list(ctx.metadata["tags"]);
        item.decisions = parse_string_list(ctx.metadata["decisions"]);

        if (auto external = ctx.metadata["external"]; external && external.IsMap()) {
            for (const auto& entry : external) {
                if (!entry.first.IsNull() && !entry.second.IsNull()) {
                    item.external[entry.first.as<std::string>()] = entry.second.as<std::string>();
                }
            }
        }

        if (auto links = ctx.metadata["links"]; links && links.IsMap()) {
            item.links.relates = parse_string_list(links["relates"]);
            item.links.blocks = parse_string_list(links["blocks"]);
            item.links.blocked_by = parse_string_list(links["blocked_by"]);
        }

        // Body sections
        auto sections = Frontmatter::parse_body_sections(ctx.body);
        if (sections.count("context")) item.context = sections["context"];
        if (sections.count("goal")) item.goal = sections["goal"];
        if (sections.count("non_goals")) item.non_goals = sections["non_goals"];
        if (sections.count("approach")) item.approach = sections["approach"];
        if (sections.count("alternatives")) item.alternatives = sections["alternatives"];
        if (sections.count("acceptance_criteria")) item.acceptance_criteria = sections["acceptance_criteria"];
        if (sections.count("risks")) item.risks = sections["risks"];
        
        if (sections.count("worklog")) {
            std::stringstream work_ss(sections["worklog"]);
            std::string line;
            while (std::getline(work_ss, line)) {
                if (!line.empty()) item.worklog.push_back(line);
            }
        }

        return item;
    } catch (const std::exception& e) {
        throw ParseError(item_path.string(), std::string("Mapping error: ") + e.what());
    }
}

void CanonicalStore::write(BacklogItem& item) const {
    auto errors = Validator::validate_schema(item);
    if (!errors.empty()) {
        std::string err_msg;
        for (const auto& e : errors) err_msg += e + "; ";
        throw ValidationError({err_msg});
    }

    if (!item.file_path) {
        throw WriteError("Item file_path is not set");
    }

    // Update timestamp
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    std::stringstream date_ss;
    date_ss << std::put_time(&buf, "%Y-%m-%d");
    item.updated = date_ss.str();

    // Build metadata node
    YAML::Node metadata;
    metadata["id"] = item.id;
    metadata["uid"] = item.uid;
    metadata["type"] = to_string(item.type);
    metadata["title"] = item.title;
    metadata["state"] = to_string(item.state);
    metadata["priority"] = item.priority ? YAML::Node(*item.priority) : YAML::Node(YAML::NodeType::Null);
    metadata["parent"] = item.parent ? YAML::Node(*item.parent) : YAML::Node(YAML::NodeType::Null);
    metadata["owner"] = item.owner ? YAML::Node(*item.owner) : YAML::Node(YAML::NodeType::Null);
    metadata["area"] = item.area ? YAML::Node(*item.area) : YAML::Node(YAML::NodeType::Null);
    metadata["iteration"] = item.iteration ? YAML::Node(*item.iteration) : YAML::Node(YAML::NodeType::Null);
    metadata["created"] = item.created;
    metadata["updated"] = item.updated;

    YAML::Node external(YAML::NodeType::Map);
    for (const auto& [key, value] : item.external) {
        external[key] = value;
    }
    metadata["external"] = external;

    YAML::Node links(YAML::NodeType::Map);
    YAML::Node relates(YAML::NodeType::Sequence);
    for (const auto& value : item.links.relates) relates.push_back(value);
    YAML::Node blocks(YAML::NodeType::Sequence);
    for (const auto& value : item.links.blocks) blocks.push_back(value);
    YAML::Node blocked_by(YAML::NodeType::Sequence);
    for (const auto& value : item.links.blocked_by) blocked_by.push_back(value);
    links["relates"] = relates;
    links["blocks"] = blocks;
    links["blocked_by"] = blocked_by;
    metadata["links"] = links;

    YAML::Node decisions(YAML::NodeType::Sequence);
    for (const auto& value : item.decisions) decisions.push_back(value);
    metadata["decisions"] = decisions;
    
    YAML::Node tags(YAML::NodeType::Sequence);
    for (const auto& t : item.tags) tags.push_back(t);
    metadata["tags"] = tags;

    // Body
    std::map<std::string, std::string> sections;
    if (item.context) sections["context"] = *item.context;
    if (item.goal) sections["goal"] = *item.goal;
    if (item.non_goals) sections["non_goals"] = *item.non_goals;
    if (item.approach) sections["approach"] = *item.approach;
    if (item.alternatives) sections["alternatives"] = *item.alternatives;
    if (item.acceptance_criteria) sections["acceptance_criteria"] = *item.acceptance_criteria;
    if (item.risks) sections["risks"] = *item.risks;
    
    if (!item.worklog.empty()) {
        std::stringstream work_ss;
        for (const auto& line : item.worklog) work_ss << line << "\n";
        sections["worklog"] = work_ss.str();
    }

    FrontmatterContext ctx;
    ctx.metadata = metadata;
    ctx.body = Frontmatter::serialize_body_sections(sections);

    std::filesystem::create_directories(item.file_path->parent_path());
    std::ofstream f(*item.file_path);
    if (!f.is_open()) {
        throw WriteError("Failed to open " + item.file_path->string() + " for writing");
    }
    f << Frontmatter::serialize(ctx);
}

BacklogItem CanonicalStore::create(const std::string& prefix, ItemType type, const std::string& title, int next_number, const std::optional<std::string>& parent) const {
    BacklogItem item;
    item.uid = generate_uuid_v7();
    
    // Abbrev mapping
    std::string abbrev;
    std::string type_dir;
    switch(type) {
        case ItemType::Epic: abbrev = "EPIC"; type_dir = "epic"; break;
        case ItemType::Feature: abbrev = "FTR"; type_dir = "feature"; break;
        case ItemType::UserStory: abbrev = "USR"; type_dir = "userstory"; break;
        case ItemType::Task: abbrev = "TSK"; type_dir = "task"; break;
        case ItemType::Bug: abbrev = "BUG"; type_dir = "bug"; break;
    }

    // ID format: PREFIX-ABBREV-0001
    std::stringstream id_ss;
    id_ss << prefix << "-" << abbrev << "-" << std::setfill('0') << std::setw(4) << next_number;
    item.id = id_ss.str();
    item.title = title;
    item.type = type;
    item.state = ItemState::New;
    item.parent = parent;

    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    std::stringstream date_ss;
    date_ss << std::put_time(&buf, "%Y-%m-%d");
    item.created = date_ss.str();
    item.updated = item.created;

    // File path: items/<type>/<bucket>/<id>_<slug>.md
    int bucket = (next_number / 100) * 100;
    std::stringstream bucket_ss;
    bucket_ss << std::setfill('0') << std::setw(4) << bucket;
    
    std::string filename = item.id + "_" + slugify(title) + ".md";
    item.file_path = items_root_ / type_dir / bucket_ss.str() / filename;

    return item;
}

std::vector<std::filesystem::path> CanonicalStore::list_items(std::optional<ItemType> type) const {
    std::vector<std::filesystem::path> results;
    if (!std::filesystem::exists(items_root_)) return results;

    auto scan_dir = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                results.push_back(entry.path());
            }
        }
    };

    if (type) {
        std::string type_dir;
        switch(*type) {
            case ItemType::Epic: type_dir = "epic"; break;
            case ItemType::Feature: type_dir = "feature"; break;
            case ItemType::UserStory: type_dir = "userstory"; break;
            case ItemType::Task: type_dir = "task"; break;
            case ItemType::Bug: type_dir = "bug"; break;
        }
        scan_dir(items_root_ / type_dir);
    } else {
        scan_dir(items_root_);
    }

    return results;
}

int CanonicalStore::get_next_id_number(ItemType type) const {
    auto items = list_items(type);
    int max_num = 0;
    
    // Pattern: prefix-<TYPE>-(\d{4})
    std::regex re(R"(-(\d{4})_)"); 
    
    for (const auto& p : items) {
        std::string name = p.filename().string();
        std::smatch match;
        if (std::regex_search(name, match, re)) {
            int num = std::stoi(match[1].str());
            if (num > max_num) max_num = num;
        }
    }
    return max_num + 1;
}

std::string CanonicalStore::slugify(const std::string& text) {
    std::string slug = text;
    std::transform(slug.begin(), slug.end(), slug.begin(), [](unsigned char c){ return std::tolower(c); });
    slug = std::regex_replace(slug, std::regex(R"([^\w\s-])"), "");
    slug = std::regex_replace(slug, std::regex(R"([-\s]+)"), "-");
    // Trim hyphens
    size_t first = slug.find_first_not_of("-");
    if (first == std::string::npos) return "";
    size_t last = slug.find_last_not_of("-");
    slug = slug.substr(first, (last - first + 1));
    if (slug.length() > 50) slug = slug.substr(0, 50);
    return slug;
}

std::string CanonicalStore::generate_uuid_v7() {
    // Simplified UUIDv7: 48-bit timestamp + 74-bit random (placeholder for exact bits)
    auto now = std::chrono::system_clock::now();
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

    uint64_t r1 = dist(gen);
    uint64_t r2 = dist(gen);

    // Draft bits for UUIDv7 (approximate)
    // ttt tttt-tttt-7rrr-yrrr-rrrr rrrr rrrr
    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (ms >> 16) << "-"
       << std::setw(4) << (ms & 0xFFFF) << "-"
       << "7" << std::setw(3) << (r1 >> 60) << "-" // marker 7
       << std::setw(1) << (8 | (r1 >> 62 & 0x3)) << std::setw(3) << (r1 & 0xFFF) << "-" // marker 8/9/a/b
       << std::setw(12) << (r2 & 0xFFFFFFFFFFFF);
    
    return ss.str();
}

} // namespace kano::backlog_core
