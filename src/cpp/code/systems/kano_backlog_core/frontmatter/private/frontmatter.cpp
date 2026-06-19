#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include <iostream>
#include <sstream>

namespace kano::backlog_core {

FrontmatterContext Frontmatter::parse(const std::string& content) {
    FrontmatterContext ctx;

    std::size_t yaml_start = std::string::npos;
    if (content.rfind("---\n", 0) == 0) {
        yaml_start = 4;
    } else if (content.rfind("---\r\n", 0) == 0) {
        yaml_start = 5;
    }

    if (yaml_start == std::string::npos) {
        ctx.body = content;
        ctx.metadata = YAML::Node(YAML::NodeType::Null);
        return ctx;
    }

    std::size_t line_start = yaml_start;
    while (line_start <= content.size()) {
        const std::size_t line_end = content.find('\n', line_start);
        const std::size_t raw_line_end = line_end == std::string::npos ? content.size() : line_end;
        std::size_t marker_end = raw_line_end;
        if (marker_end > line_start && content[marker_end - 1] == '\r') {
            --marker_end;
        }

        if (content.compare(line_start, marker_end - line_start, "---") == 0) {
            const std::string yaml_str = content.substr(yaml_start, line_start - yaml_start);
            const std::size_t body_start = line_end == std::string::npos ? content.size() : line_end + 1;
            ctx.body = content.substr(body_start);
            try {
                ctx.metadata = YAML::Load(yaml_str);
            } catch (const YAML::Exception&) {
                ctx.metadata = YAML::Node(YAML::NodeType::Null);
            }
            return ctx;
        }

        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    ctx.body = content;
    ctx.metadata = YAML::Node(YAML::NodeType::Null);
    return ctx;
}

std::string Frontmatter::serialize(const FrontmatterContext& ctx) {
    std::stringstream ss;
    if (ctx.metadata && !ctx.metadata.IsNull()) {
        ss << "---\n";
        YAML::Emitter emitter;
        emitter << ctx.metadata;
        ss << emitter.c_str() << "\n";
        ss << "---\n\n";
    }
    ss << ctx.body;
    return ss.str();
}

std::map<std::string, std::string> Frontmatter::parse_body_sections(const std::string& body) {
    std::map<std::string, std::string> sections;
    
    // Define predictable section markers for canonical body updates.
    static const std::map<std::string, std::string> section_markers = {
        {"context", "# Context"},
        {"goal", "# Goal"},
        {"non_goals", "# Non-Goals"},
        {"approach", "# Approach"},
        {"alternatives", "# Alternatives"},
        {"acceptance_criteria", "# Acceptance Criteria"},
        {"risks", "# Risks / Dependencies"},
        {"worklog", "# Worklog"}
    };

    std::stringstream ss(body);
    std::string line;
    std::string current_key;
    std::stringstream current_content;

    while (std::getline(ss, line)) {
        std::string trimmed = line;
        // Basic trim right
        size_t end = trimmed.find_last_not_of(" \n\r\t");
        if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

        bool found_marker = false;
        for (const auto& [key, marker] : section_markers) {
            if (trimmed == marker) {
                // Save previous section
                if (!current_key.empty()) {
                    std::string content = current_content.str();
                    // Trim start and end
                    size_t first = content.find_first_not_of(" \n\r\t");
                    size_t last = content.find_last_not_of(" \n\r\t");
                    if (first != std::string::npos && last != std::string::npos) {
                        sections[current_key] = content.substr(first, (last - first + 1));
                    }
                }
                current_key = key;
                current_content.str("");
                current_content.clear();
                found_marker = true;
                break;
            }
        }
        
        if (!found_marker && !current_key.empty()) {
            current_content << line << "\n";
        }
    }

    // Last section
    if (!current_key.empty()) {
        std::string content = current_content.str();
        size_t first = content.find_first_not_of(" \n\r\t");
        size_t last = content.find_last_not_of(" \n\r\t");
        if (first != std::string::npos && last != std::string::npos) {
            sections[current_key] = content.substr(first, (last - first + 1));
        }
    }

    return sections;
}

std::string Frontmatter::serialize_body_sections(const std::map<std::string, std::string>& sections) {
    std::stringstream ss;
    
    // Ordered list of sections to maintain consistency
    static const std::vector<std::pair<std::string, std::string>> ordered_sections = {
        {"context", "# Context"},
        {"goal", "# Goal"},
        {"non_goals", "# Non-Goals"},
        {"approach", "# Approach"},
        {"alternatives", "# Alternatives"},
        {"acceptance_criteria", "# Acceptance Criteria"},
        {"risks", "# Risks / Dependencies"},
        {"worklog", "# Worklog"}
    };

    bool first = true;
    for (const auto& [key, marker] : ordered_sections) {
        auto it = sections.find(key);
        if (it != sections.end() && !it->second.empty()) {
            if (!first) ss << "\n\n";
            ss << marker << "\n\n" << it->second;
            first = false;
        }
    }
    
    return ss.str();
}

} // namespace kano::backlog_core
