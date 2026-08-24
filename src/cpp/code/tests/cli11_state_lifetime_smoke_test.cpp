#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct SourceLine {
    std::size_t offset = 0;
    std::vector<std::size_t> scope;
    std::string raw;
    std::string code;
    std::vector<std::string> declared_names;
};

struct Declaration {
    std::size_t line_index = 0;
    const SourceLine* source = nullptr;
};

struct LifetimeInventory {
    std::size_t total = 0;
    std::size_t lexical = 0;
    std::size_t arena_direct = 0;
    std::size_t arena_owner = 0;
    std::size_t retained_helper_owner = 0;
    std::size_t member_alias = 0;
    std::vector<std::string> unsafe;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool is_identifier_char(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
}

std::optional<std::string> leading_identifier(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    if (begin >= value.size() || (!std::isalpha(static_cast<unsigned char>(value[begin])) && value[begin] != '_')) {
        return std::nullopt;
    }
    std::size_t end = begin + 1;
    while (end < value.size() && is_identifier_char(value[end])) {
        ++end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::string strip_non_code(std::string_view source) {
    enum class Mode { Code, LineComment, BlockComment, String, Character };
    Mode mode = Mode::Code;
    std::string result(source.size(), ' ');
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char value = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';
        if (mode == Mode::Code) {
            if (value == '/' && next == '/') {
                ++index;
                mode = Mode::LineComment;
            } else if (value == '/' && next == '*') {
                ++index;
                mode = Mode::BlockComment;
            } else if (value == '"') {
                mode = Mode::String;
            } else if (value == '\'') {
                mode = Mode::Character;
            } else {
                result[index] = value;
            }
        } else if (mode == Mode::LineComment) {
            if (value == '\n') {
                result[index] = value;
                mode = Mode::Code;
            }
        } else if (mode == Mode::BlockComment) {
            if (value == '*' && next == '/') {
                ++index;
                mode = Mode::Code;
            } else if (value == '\n') {
                result[index] = value;
            }
        } else if (value == '\\') {
            ++index;
        } else if ((mode == Mode::String && value == '"') ||
                   (mode == Mode::Character && value == '\'')) {
            mode = Mode::Code;
        } else if (value == '\n') {
            result[index] = value;
        }
    }
    return result;
}

std::vector<std::string> declared_names(std::string_view code_line) {
    std::string remainder = trim_copy(code_line);
    const std::vector<std::string> fixed_prefixes = {
        "const auto& ", "const auto* ", "const auto ",
        "auto& ", "auto* ", "auto ",
        "std::filesystem::path ", "std::string ", "std::size_t ",
        "size_t ", "double ", "float ", "bool ", "int ",
    };
    bool auto_declaration = false;
    bool matched = false;
    for (const auto& prefix : fixed_prefixes) {
        if (remainder.rfind(prefix, 0) == 0) {
            auto_declaration = prefix.find("auto") != std::string::npos;
            remainder.erase(0, prefix.size());
            matched = true;
            break;
        }
    }
    if (!matched && remainder.rfind("std::vector<", 0) == 0) {
        const auto close = remainder.find('>');
        if (close != std::string::npos) {
            remainder.erase(0, close + 1);
            remainder = trim_copy(remainder);
            matched = true;
        }
    }
    if (!matched || remainder.empty() || remainder.front() == '[') {
        return {};
    }

    std::vector<std::string> names;
    int nesting = 0;
    std::size_t segment_start = 0;
    const auto append_segment = [&](std::size_t end, std::vector<std::string>& output) {
        if (const auto name = leading_identifier(std::string_view(remainder).substr(segment_start, end - segment_start))) {
            output.push_back(*name);
        }
    };
    for (std::size_t index = 0; index <= remainder.size(); ++index) {
        const char value = index < remainder.size() ? remainder[index] : ';';
        if (value == '(' || value == '[' || value == '{' || value == '<') {
            ++nesting;
        } else if (value == ')' || value == ']' || value == '}' || value == '>') {
            nesting = std::max(0, nesting - 1);
        }
        if ((value == ',' || value == ';') && nesting == 0) {
            append_segment(index, names);
            segment_start = index + 1;
            if (auto_declaration || value == ';') {
                break;
            }
        }
    }
    return names;
}

std::vector<SourceLine> split_source_lines(const std::string& source, const std::string& code) {
    std::vector<SourceLine> lines;
    std::vector<std::size_t> scope;
    std::vector<std::size_t> line_scope;
    std::size_t line_start = 0;
    for (std::size_t index = 0; index <= code.size(); ++index) {
        if (index == code.size() || code[index] == '\n') {
            const std::size_t length = index - line_start;
            SourceLine line;
            line.offset = line_start;
            line.scope = line_scope;
            line.raw = source.substr(line_start, length);
            line.code = code.substr(line_start, length);
            line.declared_names = declared_names(line.code);
            lines.push_back(std::move(line));
            line_start = index + 1;
            line_scope = scope;
            continue;
        }
        if (code[index] == '{') {
            scope.push_back(index);
        } else if (code[index] == '}' && !scope.empty()) {
            scope.pop_back();
        }
    }
    return lines;
}

std::size_t line_index_for_offset(const std::vector<SourceLine>& lines, std::size_t offset) {
    const auto found = std::upper_bound(
        lines.begin(), lines.end(), offset,
        [](std::size_t value, const SourceLine& line) { return value < line.offset; });
    return found == lines.begin() ? 0 : static_cast<std::size_t>(std::distance(lines.begin(), found) - 1);
}

bool is_scope_prefix(const std::vector<std::size_t>& prefix, const std::vector<std::size_t>& scope) {
    return prefix.size() <= scope.size() && std::equal(prefix.begin(), prefix.end(), scope.begin());
}

std::vector<std::size_t> scope_at_offset(
    const std::vector<SourceLine>& lines,
    const std::string& code,
    std::size_t offset) {
    const auto line_index = line_index_for_offset(lines, offset);
    auto scope = lines[line_index].scope;
    for (std::size_t index = lines[line_index].offset; index < offset; ++index) {
        if (code[index] == '{') {
            scope.push_back(index);
        } else if (code[index] == '}' && !scope.empty()) {
            scope.pop_back();
        }
    }
    return scope;
}

std::optional<Declaration> find_declaration(
    const std::vector<SourceLine>& lines,
    std::string_view name,
    std::size_t before_line,
    const std::vector<std::size_t>& binding_scope) {
    for (std::size_t line_index = before_line; line_index-- > 0;) {
        const auto& line = lines[line_index];
        if (!is_scope_prefix(line.scope, binding_scope)) {
            continue;
        }
        if (std::find(line.declared_names.begin(), line.declared_names.end(), name) != line.declared_names.end()) {
            return Declaration{line_index, &line};
        }
    }
    return std::nullopt;
}

bool helper_retains_owner(
    const std::vector<SourceLine>& lines,
    std::string_view owner,
    std::size_t before_line,
    const std::vector<std::size_t>& binding_scope) {
    const std::string marker = "cli11_state.retain(" + std::string(owner) + ")";
    for (std::size_t line_index = before_line; line_index-- > 0;) {
        const auto& line = lines[line_index];
        if (!is_scope_prefix(line.scope, binding_scope)) {
            continue;
        }
        if (line.code.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> member_alias_owner(std::string_view declaration) {
    const auto equals = declaration.find('=');
    if (equals == std::string_view::npos) {
        return std::nullopt;
    }
    const auto arrow = declaration.find("->", equals + 1);
    if (arrow == std::string_view::npos) {
        return std::nullopt;
    }
    return leading_identifier(declaration.substr(equals + 1, arrow - equals - 1));
}

bool owner_is_retained(
    const std::vector<SourceLine>& lines,
    std::string_view owner,
    std::size_t binding_line,
    const std::vector<std::size_t>& binding_scope,
    const std::vector<std::size_t>& parse_scope,
    LifetimeInventory& inventory) {
    const auto declaration = find_declaration(lines, owner, binding_line, binding_scope);
    if (declaration) {
        if (is_scope_prefix(declaration->source->scope, parse_scope)) {
            ++inventory.lexical;
            return true;
        }
        if (declaration->source->code.find("cli11_state.make_shared<") != std::string::npos) {
            ++inventory.arena_owner;
            return true;
        }
    }
    if (helper_retains_owner(lines, owner, binding_line, binding_scope)) {
        ++inventory.retained_helper_owner;
        return true;
    }
    return false;
}

LifetimeInventory audit_cli11_lifetimes(const std::filesystem::path& source_path) {
    const std::string source = read_text(source_path);
    const std::string code = strip_non_code(source);
    const auto lines = split_source_lines(source, code);
    const auto parse_offset = code.find("app.parse(parse_argc, parse_argv)");
    expect(parse_offset != std::string::npos, "CLI11 parse dispatch was not found");
    const auto parse_scope = scope_at_offset(lines, code, parse_offset);

    LifetimeInventory inventory;
    std::size_t search_offset = 0;
    while (search_offset < code.size()) {
        const auto option = code.find("add_option(", search_offset);
        const auto flag = code.find("add_flag(", search_offset);
        const auto call = option == std::string::npos ? flag
            : (flag == std::string::npos ? option : std::min(option, flag));
        if (call == std::string::npos) {
            break;
        }
        ++inventory.total;
        std::size_t cursor = code.find('(', call) + 1;
        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) {
            ++cursor;
        }
        expect(cursor < source.size() && source[cursor] == '"', "CLI11 option name must be a string literal");
        for (++cursor; cursor < source.size(); ++cursor) {
            if (source[cursor] == '\\') {
                ++cursor;
            } else if (source[cursor] == '"') {
                ++cursor;
                break;
            }
        }
        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) {
            ++cursor;
        }
        expect(cursor < source.size() && source[cursor] == ',', "CLI11 binding argument was not found");
        do {
            ++cursor;
        } while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])));

        const std::size_t binding_offset = cursor;
        bool dereference = false;
        if (cursor < source.size() && source[cursor] == '*') {
            dereference = true;
            ++cursor;
        }
        const auto owner = leading_identifier(std::string_view(source).substr(cursor));
        expect(owner.has_value(), "CLI11 binding must start with an identifier");
        cursor += owner->size();
        bool member = false;
        if (cursor + 1 < source.size() && source[cursor] == '-' && source[cursor + 1] == '>') {
            member = true;
            cursor += 2;
            const auto field = leading_identifier(std::string_view(source).substr(cursor));
            expect(field.has_value(), "CLI11 member binding must name a field");
            cursor += field->size();
        }

        const auto binding_line = line_index_for_offset(lines, binding_offset);
        const auto binding_scope = scope_at_offset(lines, code, binding_offset);
        bool safe = false;
        if (dereference || member) {
            safe = owner_is_retained(
                lines, *owner, binding_line, binding_scope, parse_scope, inventory);
        } else {
            const auto declaration = find_declaration(
                lines, *owner, binding_line, binding_scope);
            if (declaration && is_scope_prefix(declaration->source->scope, parse_scope)) {
                ++inventory.lexical;
                safe = true;
            } else if (declaration && declaration->source->code.find("cli11_state.keep<") != std::string::npos) {
                ++inventory.arena_direct;
                safe = true;
            } else if (declaration) {
                if (const auto alias_owner = member_alias_owner(declaration->source->code)) {
                    safe = owner_is_retained(
                        lines, *alias_owner, declaration->line_index,
                        declaration->source->scope, parse_scope, inventory);
                    if (safe) {
                        ++inventory.member_alias;
                    }
                }
            }
        }
        if (!safe) {
            inventory.unsafe.push_back(
                "line " + std::to_string(binding_line + 1) + ": " + *owner);
        }
        search_offset = cursor;
    }
    return inventory;
}

} // namespace

int main() {
    try {
        const auto source_path = std::filesystem::path(KANO_REPO_ROOT) /
            "src/cpp/code/apps/kano_backlog_cli/main.cpp";
        const auto inventory = audit_cli11_lifetimes(source_path);
        expect(inventory.total == 696, "CLI11 binding inventory changed; review every added or removed binding and update the audited baseline");
        expect(inventory.lexical > 0, "CLI11 audit did not classify lexical main-lifetime bindings");
        expect(inventory.arena_direct > 0, "CLI11 audit did not classify arena-retained direct bindings");
        expect(inventory.arena_owner > 0, "CLI11 audit did not classify arena-retained shared owners");
        expect(inventory.retained_helper_owner > 0, "CLI11 audit did not classify helper-retained shared owners");
        expect(inventory.member_alias > 0, "CLI11 audit did not classify shared-state member aliases");
        if (!inventory.unsafe.empty()) {
            std::ostringstream message;
            message << "unsafe CLI11 option bindings:";
            for (const auto& entry : inventory.unsafe) {
                message << "\n- " << entry;
            }
            throw std::runtime_error(message.str());
        }
        std::cout << "cli11_state_lifetime_smoke_test: PASS"
                  << " total=" << inventory.total
                  << " lexical=" << inventory.lexical
                  << " arena_direct=" << inventory.arena_direct
                  << " arena_owner=" << inventory.arena_owner
                  << " helper_owner=" << inventory.retained_helper_owner
                  << " member_alias=" << inventory.member_alias
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cli11_state_lifetime_smoke_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
