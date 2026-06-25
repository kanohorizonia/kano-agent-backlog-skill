#include "kano/backlog_core/refs/ref_parser.hpp"
#include <algorithm>
#include <cctype>

namespace kano::backlog_core {

// Helper: trim whitespace
static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \n\r\t");
    return s.substr(first, (last - first + 1));
}

std::optional<ParsedRef> RefParser::parse(const std::string& ref) {
    auto p_ref = parse_path(ref);
    if (p_ref) return *p_ref;

    auto d_ref = parse_display_id(ref);
    if (d_ref) return *d_ref;

    auto a_ref = parse_adr(ref);
    if (a_ref) return *a_ref;

    auto u_ref = parse_uuid(ref);
    if (u_ref) return *u_ref;

    return std::nullopt;
}

std::optional<PathRef> RefParser::parse_path(const std::string& ref) {
    std::string s = trim(ref);
    if (s.empty()) {
        return std::nullopt;
    }

    const bool looks_like_windows_drive =
        s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '\\' || s[2] == '/');
    const bool looks_like_relative =
        s.rfind("./", 0) == 0 || s.rfind("../", 0) == 0 || s.rfind(".\\", 0) == 0 || s.rfind("..\\", 0) == 0;
    const bool has_separator = s.find('/') != std::string::npos || s.find('\\') != std::string::npos;
    const bool looks_like_markdown_file = s.size() >= 3 && s.substr(s.size() - 3) == ".md";

    if (looks_like_windows_drive || looks_like_relative || has_separator || looks_like_markdown_file) {
        PathRef res;
        res.path = s;
        res.raw = s;
        return res;
    }

    return std::nullopt;
}

std::optional<DisplayIdRef> RefParser::parse_display_id(const std::string& ref) {
    std::string s = trim(ref);
    const size_t number_dash = s.rfind('-');
    if (number_dash == std::string::npos || number_dash + 5 != s.size()) {
        return std::nullopt;
    }

    const std::string number_part = s.substr(number_dash + 1);
    if (!std::all_of(number_part.begin(), number_part.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        return std::nullopt;
    }

    const std::string product_and_type = s.substr(0, number_dash);
    const size_t type_dash = product_and_type.rfind('-');
    if (type_dash == std::string::npos) {
        return std::nullopt;
    }

    const std::string product = product_and_type.substr(0, type_dash);
    const std::string type_abbrev = product_and_type.substr(type_dash + 1);
    const bool type_ok =
        type_abbrev == "INIT" || type_abbrev == "EPIC" || type_abbrev == "FTR" || type_abbrev == "USR" ||
        type_abbrev == "TSK" || type_abbrev == "BUG" || type_abbrev == "ISS";
    if (!type_ok) {
        return std::nullopt;
    }

    if (product.size() < 2 || product.size() > 16 || !std::isupper(static_cast<unsigned char>(product.front()))) {
        return std::nullopt;
    }
    if (!std::all_of(product.begin(), product.end(), [](unsigned char ch) {
            return std::isupper(ch) || std::isdigit(ch);
        })) {
        return std::nullopt;
    }

    DisplayIdRef res;
    res.product = product;
    res.type_abbrev = type_abbrev;
    res.number = std::stoi(number_part);
    res.raw = s;
    return res;
}

std::optional<AdrRef> RefParser::parse_adr(const std::string& ref) {
    std::string s = trim(ref);
    constexpr const char* prefix = "ADR-";
    if (s.rfind(prefix, 0) != 0 || s.size() < 8) {
        return std::nullopt;
    }

    const std::string number_part = s.substr(4, 4);
    if (!std::all_of(number_part.begin(), number_part.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
        return std::nullopt;
    }

    AdrRef res;
    res.number = std::stoi(number_part);
    res.raw = s;

    if (s.size() == 8) {
        return res;
    }

    constexpr const char* appendix_prefix = "-appendix_";
    const std::string suffix = s.substr(8);
    if (suffix.rfind(appendix_prefix, 0) != 0) {
        return std::nullopt;
    }
    const std::string appendix = suffix.substr(10);
    if (appendix.empty()) {
        return std::nullopt;
    }
    if (!std::all_of(appendix.begin(), appendix.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || std::isdigit(ch) || ch == '_' || ch == '-';
        })) {
        return std::nullopt;
    }
    res.appendix = appendix;
    return res;
}

std::optional<UuidRef> RefParser::parse_uuid(const std::string& ref) {
    std::string s = trim(ref);
    if (s.size() != 36) {
        return std::nullopt;
    }

    const auto is_lower_hex = [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const bool hyphen_pos = i == 8 || i == 13 || i == 18 || i == 23;
        if (hyphen_pos) {
            if (s[i] != '-') {
                return std::nullopt;
            }
            continue;
        }
        if (!is_lower_hex(s[i])) {
            return std::nullopt;
        }
    }
    if (s[14] != '7' || (s[19] != '8' && s[19] != '9' && s[19] != 'a' && s[19] != 'b')) {
        return std::nullopt;
    }

    UuidRef res;
    res.uuid = s;
    res.raw = s;
    return res;
}

} // namespace kano::backlog_core
