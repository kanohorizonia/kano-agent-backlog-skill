#include "parser.hpp"

#include "../errors/errors.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace kano::backlog::core {

namespace {

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            stream << '\n';
        }
        stream << lines[i];
    }
    return stream.str();
}

} // namespace

std::vector<std::string> FrontmatterParser::split_lines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string FrontmatterParser::trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string FrontmatterParser::unquote(const std::string& value) {
    std::string trimmed = trim(value);
    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '\'' && trimmed.back() == '\'') ||
         (trimmed.front() == '"' && trimmed.back() == '"'))) {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::vector<std::string> FrontmatterParser::parse_string_list(const std::string& value) {
    std::string trimmed = trim(value);
    if (trimmed.empty() || trimmed == "[]") {
        return {};
    }

    if (trimmed.front() == '[' && trimmed.back() == ']') {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }

    std::vector<std::string> result;
    std::stringstream stream(trimmed);
    std::string token;
    while (std::getline(stream, token, ',')) {
        std::string item = unquote(token);
        if (!item.empty() && item != "null") {
            result.push_back(item);
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> FrontmatterParser::parse_nested_map(
    const std::vector<std::string>& lines,
    std::size_t& index) {
    std::unordered_map<std::string, std::string> values;
    while (index < lines.size()) {
        const std::string& raw = lines[index];
        if (!starts_with(raw, "  ")) {
            break;
        }

        std::string line = trim(raw);
        std::size_t sep = line.find(':');
        if (sep == std::string::npos) {
            ++index;
            continue;
        }

        std::string key = trim(line.substr(0, sep));
        std::string value = unquote(line.substr(sep + 1));
        values[key] = value;
        ++index;
    }
    return values;
}

std::unordered_map<std::string, std::vector<std::string>> FrontmatterParser::parse_nested_list_map(
    const std::vector<std::string>& lines,
    std::size_t& index) {
    std::unordered_map<std::string, std::vector<std::string>> values;
    while (index < lines.size()) {
        const std::string& raw = lines[index];
        if (!starts_with(raw, "  ")) {
            break;
        }

        std::string line = trim(raw);
        std::size_t sep = line.find(':');
        if (sep == std::string::npos) {
            ++index;
            continue;
        }

        std::string key = trim(line.substr(0, sep));
        std::string value = trim(line.substr(sep + 1));
        values[key] = parse_string_list(value);
        ++index;
    }
    return values;
}

std::unordered_map<std::string, std::string> FrontmatterParser::extract_frontmatter(
    const std::vector<std::string>& lines) {
    std::unordered_map<std::string, std::string> fields;
    if (lines.size() < 3 || trim(lines[0]) != "---") {
        throw ParseError("Missing frontmatter opening delimiter");
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& raw = lines[i];
        if (trim(raw) == "---") {
            break;
        }

        if (trim(raw).empty()) {
            continue;
        }

        if (starts_with(raw, "  ")) {
            continue;
        }

        std::size_t sep = raw.find(':');
        if (sep == std::string::npos) {
            continue;
        }

        std::string key = trim(raw.substr(0, sep));
        std::string value = trim(raw.substr(sep + 1));
        fields[key] = unquote(value);
    }

    return fields;
}

void FrontmatterParser::parse_body_sections(BacklogItem& item, const std::vector<std::string>& body_lines) {
    std::string current_section;
    std::vector<std::string> buffer;

    auto flush = [&]() {
        if (current_section.empty()) {
            return;
        }

        std::string content = trim(join_lines(buffer));
        if (current_section == "Context") {
            item.context = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Goal") {
            item.goal = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Non-Goals") {
            item.non_goals = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Approach") {
            item.approach = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Alternatives") {
            item.alternatives = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Acceptance Criteria") {
            item.acceptance_criteria = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Risks / Dependencies") {
            item.risks = content.empty() ? std::optional<std::string>{} : std::make_optional(content);
        } else if (current_section == "Worklog") {
            item.worklog.clear();
            std::stringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                std::string trimmed = trim(line);
                if (!trimmed.empty()) {
                    item.worklog.push_back(trimmed);
                }
            }
        }
    };

    for (const std::string& raw : body_lines) {
        std::string line = trim(raw);
        if (starts_with(line, "# ")) {
            flush();
            current_section = trim(line.substr(2));
            buffer.clear();
            continue;
        }

        if (!current_section.empty()) {
            buffer.push_back(raw);
        }
    }

    flush();
}

std::string FrontmatterParser::format_string_list(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "[]";
    }

    std::ostringstream stream;
    stream << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << ", ";
        }
        stream << '\'' << values[i] << '\'';
    }
    stream << ']';
    return stream.str();
}

BacklogItem FrontmatterParser::parse(const std::string& markdown_content) {
    std::vector<std::string> lines = split_lines(markdown_content);
    if (lines.size() < 3 || trim(lines[0]) != "---") {
        throw ParseError("Missing YAML frontmatter");
    }

    std::size_t closing = std::string::npos;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "---") {
            closing = i;
            break;
        }
    }
    if (closing == std::string::npos) {
        throw ParseError("Missing frontmatter closing delimiter");
    }

    BacklogItem item;
    std::size_t i = 1;
    while (i < closing) {
        const std::string& raw = lines[i];
        if (trim(raw).empty()) {
            ++i;
            continue;
        }
        if (starts_with(raw, "  ")) {
            ++i;
            continue;
        }

        std::size_t sep = raw.find(':');
        if (sep == std::string::npos) {
            ++i;
            continue;
        }

        std::string key = trim(raw.substr(0, sep));
        std::string value = trim(raw.substr(sep + 1));
        ++i;

        if (key == "id") {
            item.id = unquote(value);
        } else if (key == "uid") {
            item.uid = unquote(value);
        } else if (key == "type") {
            item.type = parse_type(unquote(value));
        } else if (key == "title") {
            item.title = unquote(value);
        } else if (key == "state") {
            item.state = parse_state(unquote(value));
        } else if (key == "priority") {
            std::string parsed = unquote(value);
            if (!parsed.empty() && parsed != "null") {
                item.priority = parsed;
            }
        } else if (key == "parent") {
            std::string parsed = unquote(value);
            if (!parsed.empty() && parsed != "null") {
                item.parent = parsed;
            }
        } else if (key == "owner") {
            std::string parsed = unquote(value);
            if (!parsed.empty() && parsed != "null" && parsed != "None") {
                item.owner = parsed;
            }
        } else if (key == "tags") {
            item.tags = parse_string_list(value);
        } else if (key == "created") {
            item.created = unquote(value);
        } else if (key == "updated") {
            item.updated = unquote(value);
        } else if (key == "area") {
            std::string parsed = unquote(value);
            if (!parsed.empty() && parsed != "null") {
                item.area = parsed;
            }
        } else if (key == "iteration") {
            std::string parsed = unquote(value);
            if (!parsed.empty() && parsed != "null") {
                item.iteration = parsed;
            }
        } else if (key == "decisions") {
            item.decisions = parse_string_list(value);
        } else if (key == "external") {
            item.external = parse_nested_map(lines, i);
        } else if (key == "links") {
            item.links = parse_nested_list_map(lines, i);
        }
    }

    if (item.links.empty()) {
        item.links = {{"relates", {}}, {"blocks", {}}, {"blocked_by", {}}};
    }
    if (item.external.empty()) {
        item.external = {{"azure_id", "null"}, {"jira_key", "null"}};
    }

    std::vector<std::string> body_lines;
    if (closing + 1 < lines.size()) {
        body_lines.assign(lines.begin() + static_cast<std::ptrdiff_t>(closing + 1), lines.end());
    }
    parse_body_sections(item, body_lines);
    return item;
}

std::string FrontmatterParser::serialize(const BacklogItem& item) {
    std::ostringstream out;
    out << "---\n";
    out << "area: " << (item.area.has_value() ? *item.area : "general") << "\n";
    out << "created: '" << item.created << "'\n";
    out << "decisions: " << format_string_list(item.decisions) << "\n";
    out << "external:\n";
    auto azure = item.external.contains("azure_id") ? item.external.at("azure_id") : "null";
    auto jira = item.external.contains("jira_key") ? item.external.at("jira_key") : "null";
    out << "  azure_id: " << azure << "\n";
    out << "  jira_key: " << jira << "\n";
    out << "id: " << item.id << "\n";
    out << "iteration: " << (item.iteration.has_value() ? *item.iteration : "backlog") << "\n";
    out << "links:\n";
    const auto format_link = [&](const std::string& key) {
        auto it = item.links.find(key);
        return it == item.links.end() ? std::string("[]") : format_string_list(it->second);
    };
    out << "  blocked_by: " << format_link("blocked_by") << "\n";
    out << "  blocks: " << format_link("blocks") << "\n";
    out << "  relates: " << format_link("relates") << "\n";
    out << "owner: " << (item.owner.has_value() ? *item.owner : "None") << "\n";
    out << "parent: " << (item.parent.has_value() ? *item.parent : "null") << "\n";
    out << "priority: " << (item.priority.has_value() ? *item.priority : "P2") << "\n";
    out << "state: " << to_string(item.state) << "\n";
    out << "tags: " << format_string_list(item.tags) << "\n";
    out << "title: '" << item.title << "'\n";
    out << "type: " << to_string(item.type) << "\n";
    out << "uid: " << item.uid << "\n";
    out << "updated: '" << item.updated << "'\n";
    out << "---\n\n";

    auto write_section = [&](const char* heading, const std::optional<std::string>& value) {
        if (value.has_value() && !trim(*value).empty()) {
            out << "# " << heading << "\n\n" << *value << "\n\n";
        }
    };

    write_section("Context", item.context);
    write_section("Goal", item.goal);
    write_section("Non-Goals", item.non_goals);
    write_section("Approach", item.approach);
    write_section("Alternatives", item.alternatives);
    write_section("Acceptance Criteria", item.acceptance_criteria);
    write_section("Risks / Dependencies", item.risks);
    out << "# Worklog\n\n";
    for (const std::string& entry : item.worklog) {
        out << entry << "\n";
    }
    return out.str();
}

ItemType FrontmatterParser::parse_type(const std::string& type_str) {
    if (type_str == "Epic") return ItemType::Epic;
    if (type_str == "Feature") return ItemType::Feature;
    if (type_str == "UserStory") return ItemType::UserStory;
    if (type_str == "Task") return ItemType::Task;
    if (type_str == "Bug") return ItemType::Bug;
    throw ParseError("Unknown item type: " + type_str);
}

ItemState FrontmatterParser::parse_state(const std::string& state_str) {
    if (state_str == "New") return ItemState::New;
    if (state_str == "Proposed") return ItemState::Proposed;
    if (state_str == "Planned") return ItemState::Planned;
    if (state_str == "Ready") return ItemState::Ready;
    if (state_str == "InProgress" || state_str == "In Progress") return ItemState::InProgress;
    if (state_str == "Review") return ItemState::Review;
    if (state_str == "Done" || state_str == "Completed") return ItemState::Done;
    if (state_str == "Blocked") return ItemState::Blocked;
    if (state_str == "Dropped" || state_str == "Cancelled") return ItemState::Dropped;
    throw ParseError("Unknown item state: " + state_str);
}

} // namespace kano::backlog::core
