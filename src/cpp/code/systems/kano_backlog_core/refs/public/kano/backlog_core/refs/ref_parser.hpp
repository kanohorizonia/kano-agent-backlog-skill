#pragma once

#include <string>
#include <optional>
#include <variant>
#include <vector>

namespace kano::backlog_core {

enum class RefType {
    DisplayId,
    Adr,
    Uuid,
    Path,
    Unknown
};

struct DisplayIdRef {
    std::string product;
    std::string type_abbrev;
    int number;
    std::string raw;
};

struct AdrRef {
    int number;
    std::optional<std::string> appendix;
    std::string raw;
};

struct UuidRef {
    std::string uuid;
    std::string raw;
};

struct PathRef {
    std::string path;
    std::string raw;
};

using ParsedRef = std::variant<DisplayIdRef, AdrRef, UuidRef, PathRef>;

class RefParser {
public:
    static std::optional<ParsedRef> parse(const std::string& ref);
    
    // Internal parsers
    static std::optional<DisplayIdRef> parse_display_id(const std::string& ref);
    static std::optional<AdrRef> parse_adr(const std::string& ref);
    static std::optional<UuidRef> parse_uuid(const std::string& ref);
    static std::optional<PathRef> parse_path(const std::string& ref);
};

} // namespace kano::backlog_core
