#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include <iostream>
#include <sstream>
#include <regex>

namespace kano::backlog_core {

FrontmatterContext Frontmatter::parse(const std::string& content) {
    FrontmatterContext ctx;
    
    // Pattern for frontmatter: starts with ---, ends with ---
    // Using a simple state-based parser or regex
    static const std::regex fm_regex(R"(^---\r?\n([\s\S]*?)\r?\n---\r?\n([\s\S]*)$)");
    std::smatch match;
    
    if (std::regex_match(content, match, fm_regex)) {
        std::string yaml_str = match[1].str();
        ctx.body = match[2].str();
        try {
            ctx.metadata = YAML::Load(yaml_str);
        } catch (const YAML::Exception& e) {
            // If YAML parsing fails, metadata will be Null node
            ctx.metadata = YAML::Node(YAML::NodeType::Null);
        }
    } else {
        // No frontmatter found, whole content is body
        ctx.body = content;
        ctx.metadata = YAML::Node(YAML::NodeType::Null);
    }
    
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
