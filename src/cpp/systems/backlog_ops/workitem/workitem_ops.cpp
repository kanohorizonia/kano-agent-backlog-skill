#include "workitem_ops.hpp"

#include "../../backlog_core/errors/errors.hpp"
#include "../../backlog_core/frontmatter/parser.hpp"
#include "../../backlog_core/state/state_machine.hpp"
#include "../../backlog_core/validation/validator.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>

namespace kano::backlog::ops {

namespace {

std::string join_initials(const std::vector<std::string>& parts) {
    std::string prefix;
    for (const std::string& part : parts) {
        if (!part.empty()) {
            prefix.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(part.front()))));
        }
    }
    return prefix;
}

} // namespace

WorkitemOps::WorkitemOps(
    std::shared_ptr<adapters::FilesystemAdapter> fs,
    std::filesystem::path product_root)
    : fs_(std::move(fs)), product_root_(std::move(product_root)), items_root_(product_root_ / "items") {}

std::string WorkitemOps::derive_prefix(const std::string& product) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : product) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty() && std::isupper(static_cast<unsigned char>(ch)) != 0 &&
                std::islower(static_cast<unsigned char>(current.back())) != 0) {
                parts.push_back(current);
                current.clear();
            }
            current.push_back(ch);
        } else if (!current.empty()) {
            parts.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    std::string prefix = join_initials(parts);
    if (prefix.size() == 1 && !parts.empty()) {
        for (char ch : parts.front()) {
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (std::string("AEIOU").find(upper) == std::string::npos && upper != prefix[0]) {
                prefix.push_back(upper);
                break;
            }
        }
    }
    if (prefix.size() < 2) {
        std::string compact;
        for (char ch : product) {
            if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
                compact.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            }
        }
        prefix = compact.substr(0, std::min<std::size_t>(2, compact.size()));
    }
    return prefix;
}

std::string WorkitemOps::slugify(const std::string& title) {
    std::string slug;
    bool last_dash = false;
    for (char ch : title) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            last_dash = false;
        } else if (!last_dash) {
            slug.push_back('-');
            last_dash = true;
        }
    }
    while (!slug.empty() && slug.front() == '-') slug.erase(slug.begin());
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) {
        return "untitled";
    }
    if (slug.size() > 80) {
        slug.resize(80);
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
    }
    return slug;
}

std::string WorkitemOps::generate_uuid_v7_like() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<unsigned long long> dist(0, 0xffffffffffffffffULL);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto millis = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    unsigned long long rand_a = dist(gen);
    unsigned long long rand_b = dist(gen);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    out << std::setw(8) << ((millis >> 16) & 0xffffffffULL) << '-';
    out << std::setw(4) << (millis & 0xffffULL) << '-';
    out << '7' << std::setw(3) << (rand_a & 0x0fffULL) << '-';
    out << std::setw(1) << (((rand_a >> 12) & 0x3ULL) + 8) << std::setw(3) << ((rand_a >> 14) & 0x0fffULL) << '-';
    out << std::setw(12) << (rand_b & 0xffffffffffffULL);
    return out.str();
}

std::string WorkitemOps::iso_date_today() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif
    std::ostringstream out;
    out << std::put_time(&local_tm, "%Y-%m-%d");
    return out.str();
}

std::string WorkitemOps::type_code(core::ItemType type) {
    switch (type) {
    case core::ItemType::Epic: return "EPIC";
    case core::ItemType::Feature: return "FTR";
    case core::ItemType::UserStory: return "USR";
    case core::ItemType::Task: return "TSK";
    case core::ItemType::Bug: return "BUG";
    }
    return "TSK";
}

std::string WorkitemOps::type_dirname(core::ItemType type) {
    switch (type) {
    case core::ItemType::Epic: return "epic";
    case core::ItemType::Feature: return "feature";
    case core::ItemType::UserStory: return "userstory";
    case core::ItemType::Task: return "task";
    case core::ItemType::Bug: return "bug";
    }
    return "task";
}

std::string WorkitemOps::generate_id(core::ItemType type, const std::string& product) const {
    std::string prefix = derive_prefix(product);
    std::regex pattern(prefix + "-" + type_code(type) + R"(-(\d{4}))");
    int max_number = 0;

    for (const auto& path : fs_->list_items(items_root_)) {
        const std::string name = path.filename().string();
        if (name == "README.md" || name.ends_with(".index.md")) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(name, match, pattern)) {
            max_number = std::max(max_number, std::stoi(match[1].str()));
        }
    }

    std::ostringstream out;
    out << prefix << '-' << type_code(type) << '-' << std::setw(4) << std::setfill('0') << (max_number + 1);
    return out.str();
}

std::filesystem::path WorkitemOps::build_item_path(
    core::ItemType type,
    const std::string& item_id,
    const std::string& title) const {
    std::regex pattern(R"(-(\d{4})$)");
    std::smatch match;
    int number = 1;
    if (std::regex_search(item_id, match, pattern)) {
        number = std::stoi(match[1].str());
    }
    int bucket = (number / 100) * 100;
    std::ostringstream bucket_name;
    bucket_name << std::setw(4) << std::setfill('0') << bucket;

    return items_root_ / type_dirname(type) / bucket_name.str() /
           (item_id + "_" + slugify(title) + ".md");
}

std::optional<std::filesystem::path> WorkitemOps::find_item_path(const std::string& item_id) const {
    for (const auto& path : fs_->list_items(items_root_)) {
        const std::string name = path.filename().string();
        if (name == "README.md" || name.ends_with(".index.md")) {
            continue;
        }
        if (name.rfind(item_id + "_", 0) == 0) {
            return path;
        }
    }
    return std::nullopt;
}

core::BacklogItem WorkitemOps::create_item(
    core::ItemType type,
    const std::string& title,
    const std::string& product,
    const std::string& agent) {
    core::BacklogItem item;
    item.id = generate_id(type, product);
    item.uid = generate_uuid_v7_like();
    item.type = type;
    item.title = title;
    item.state = core::ItemState::Proposed;
    item.priority = "P2";
    item.area = "general";
    item.iteration = "backlog";
    item.external = {{"azure_id", "null"}, {"jira_key", "null"}};
    item.links = {{"relates", {}}, {"blocks", {}}, {"blocked_by", {}}};
    item.created = iso_date_today();
    item.updated = item.created;
    item.worklog.push_back(item.created + " 00:00 [agent=" + agent + "] Created item via native C++ CLI");

    const auto validation = core::Validator::validate_item(item);
    if (!validation.valid) {
        throw core::ValidationError("New work item failed validation");
    }

    std::filesystem::path output_path = build_item_path(type, item.id, title);
    if (!fs_->write_file(output_path, core::FrontmatterParser::serialize(item))) {
        throw core::BacklogError("Failed to write item: " + output_path.string());
    }
    return item;
}

bool WorkitemOps::update_state(const std::string& item_id, core::ItemState new_state) {
    auto path = find_item_path(item_id);
    if (!path.has_value()) {
        return false;
    }

    auto content = fs_->read_file(*path);
    if (!content.has_value()) {
        return false;
    }

    core::BacklogItem item = core::FrontmatterParser::parse(*content);
    if (!core::StateMachine::can_transition(item.state, new_state) && item.state != new_state) {
        return false;
    }

    item.state = new_state;
    item.updated = iso_date_today();

    if (new_state == core::ItemState::Ready || new_state == core::ItemState::InProgress ||
        new_state == core::ItemState::Review || new_state == core::ItemState::Done) {
        const auto validation = core::Validator::validate_item(item);
        if (!validation.valid) {
            return false;
        }
    }

    return fs_->write_file(*path, core::FrontmatterParser::serialize(item));
}

std::optional<core::BacklogItem> WorkitemOps::get_item(const std::string& item_id) {
    auto path = find_item_path(item_id);
    if (!path.has_value()) {
        return std::nullopt;
    }

    auto content = fs_->read_file(*path);
    if (!content.has_value()) {
        return std::nullopt;
    }

    return core::FrontmatterParser::parse(*content);
}

bool WorkitemOps::append_worklog(
    const std::string& item_id,
    const std::string& message,
    const std::string& agent) {
    auto path = find_item_path(item_id);
    if (!path.has_value()) {
        return false;
    }

    auto content = fs_->read_file(*path);
    if (!content.has_value()) {
        return false;
    }

    core::BacklogItem item = core::FrontmatterParser::parse(*content);
    item.updated = iso_date_today();
    item.worklog.push_back(item.updated + " 00:00 [agent=" + agent + "] " + message);
    return fs_->write_file(*path, core::FrontmatterParser::serialize(item));
}

bool WorkitemOps::add_decision(
    const std::string& item_id,
    const std::string& decision,
    const std::optional<std::string>& source,
    const std::string& agent) {
    auto path = find_item_path(item_id);
    if (!path.has_value()) {
        return false;
    }

    auto content = fs_->read_file(*path);
    if (!content.has_value()) {
        return false;
    }

    core::BacklogItem item = core::FrontmatterParser::parse(*content);
    item.updated = iso_date_today();
    item.decisions.push_back(decision);

    std::string entry = item.updated + " 00:00 [agent=" + agent + "] Added decision write-back";
    if (source.has_value() && !source->empty()) {
        entry += ": " + *source;
    }
    item.worklog.push_back(entry);
    return fs_->write_file(*path, core::FrontmatterParser::serialize(item));
}

bool WorkitemOps::attach_artifact(
    const std::string& item_id,
    const std::filesystem::path& source_path,
    bool shared,
    const std::optional<std::string>& note,
    const std::string& agent,
    std::filesystem::path* destination_out) {
    auto item_path = find_item_path(item_id);
    if (!item_path.has_value()) {
        return false;
    }

    std::filesystem::path resolved_source = std::filesystem::absolute(source_path);
    if (!std::filesystem::exists(resolved_source) || !std::filesystem::is_regular_file(resolved_source)) {
        return false;
    }

    auto content = fs_->read_file(*item_path);
    if (!content.has_value()) {
        return false;
    }

    core::BacklogItem item = core::FrontmatterParser::parse(*content);

    std::filesystem::path platform_root = product_root_;
    if (product_root_.parent_path().filename() == "products") {
        platform_root = product_root_.parent_path().parent_path();
    }

    std::filesystem::path base_root = shared ? (platform_root / "_shared" / "artifacts") : (product_root_ / "artifacts");
    std::filesystem::path dest_dir = base_root / item.id;
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        return false;
    }

    std::filesystem::path destination = dest_dir / resolved_source.filename();
    if (std::filesystem::exists(destination)) {
        std::string stem = destination.stem().string();
        std::string ext = destination.extension().string();
        int suffix = 1;
        while (std::filesystem::exists(dest_dir / (stem + "-" + std::to_string(suffix) + ext))) {
            ++suffix;
        }
        destination = dest_dir / (stem + "-" + std::to_string(suffix) + ext);
    }

    std::filesystem::copy_file(resolved_source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }

    std::filesystem::path rel_link = std::filesystem::relative(destination, item_path->parent_path(), ec);
    std::string link_path = ec ? destination.string() : rel_link.generic_string();
    std::string message = "Artifact attached: [" + destination.filename().string() + "](" + link_path + ")";
    if (note.has_value() && !note->empty()) {
        message += " - " + *note;
    }

    item.updated = iso_date_today();
    item.worklog.push_back(item.updated + " 00:00 [agent=" + agent + "] " + message);
    if (!fs_->write_file(*item_path, core::FrontmatterParser::serialize(item))) {
        return false;
    }

    if (destination_out != nullptr) {
        *destination_out = destination;
    }
    return true;
}

bool WorkitemOps::set_ready_fields(
    const std::string& item_id,
    const std::optional<std::string>& context,
    const std::optional<std::string>& goal,
    const std::optional<std::string>& approach,
    const std::optional<std::string>& acceptance_criteria,
    const std::optional<std::string>& risks,
    const std::string& agent) {
    auto path = find_item_path(item_id);
    if (!path.has_value()) {
        return false;
    }

    auto content = fs_->read_file(*path);
    if (!content.has_value()) {
        return false;
    }

    core::BacklogItem item = core::FrontmatterParser::parse(*content);
    std::vector<std::string> updated_fields;

    if (context.has_value()) {
        item.context = context;
        updated_fields.push_back("Context");
    }
    if (goal.has_value()) {
        item.goal = goal;
        updated_fields.push_back("Goal");
    }
    if (approach.has_value()) {
        item.approach = approach;
        updated_fields.push_back("Approach");
    }
    if (acceptance_criteria.has_value()) {
        item.acceptance_criteria = acceptance_criteria;
        updated_fields.push_back("Acceptance Criteria");
    }
    if (risks.has_value()) {
        item.risks = risks;
        updated_fields.push_back("Risks / Dependencies");
    }

    item.updated = iso_date_today();
    if (!updated_fields.empty()) {
        std::ostringstream entry;
        entry << item.updated << " 00:00 [agent=" << agent << "] Updated Ready fields: ";
        for (std::size_t i = 0; i < updated_fields.size(); ++i) {
            if (i > 0) {
                entry << ", ";
            }
            entry << updated_fields[i];
        }
        item.worklog.push_back(entry.str());
    }

    return fs_->write_file(*path, core::FrontmatterParser::serialize(item));
}

std::vector<core::BacklogItem> WorkitemOps::list_items() {
    std::vector<core::BacklogItem> items;
    for (const auto& path : fs_->list_items(items_root_)) {
        const std::string name = path.filename().string();
        if (name == "README.md" || name.ends_with(".index.md")) {
            continue;
        }

        auto content = fs_->read_file(path);
        if (!content.has_value()) {
            continue;
        }

        try {
            items.push_back(core::FrontmatterParser::parse(*content));
        } catch (...) {
            continue;
        }
    }

    std::sort(items.begin(), items.end(), [](const core::BacklogItem& left, const core::BacklogItem& right) {
        return left.id < right.id;
    });
    return items;
}

} // namespace kano::backlog::ops
