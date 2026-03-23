#pragma once
#include "../model/backlog_item.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace kano::backlog::core {

class FrontmatterParser {
public:
    static BacklogItem parse(const std::string& markdown_content);
    static std::string serialize(const BacklogItem& item);
    static ItemType parse_type(const std::string& type_str);
    static ItemState parse_state(const std::string& state_str);
    
private:
    static std::vector<std::string> split_lines(const std::string& content);
    static std::string trim(const std::string& value);
    static std::string unquote(const std::string& value);
    static std::vector<std::string> parse_string_list(const std::string& value);
    static std::unordered_map<std::string, std::string> extract_frontmatter(const std::vector<std::string>& lines);
    static std::unordered_map<std::string, std::string> parse_nested_map(
        const std::vector<std::string>& lines,
        std::size_t& index);
    static std::unordered_map<std::string, std::vector<std::string>> parse_nested_list_map(
        const std::vector<std::string>& lines,
        std::size_t& index);
    static void parse_body_sections(BacklogItem& item, const std::vector<std::string>& body_lines);
    static std::string format_string_list(const std::vector<std::string>& values);
};

} // namespace kano::backlog::core
