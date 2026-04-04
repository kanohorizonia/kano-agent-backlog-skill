#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <regex>
#include <set>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kano::backlog_core {

RefResolver::RefResolver(const CanonicalStore& canonical) : canonical_(canonical) {}

BacklogItem RefResolver::resolve(const std::string& ref) const {
    auto parsed = RefParser::parse(ref);
    if (!parsed) {
        throw ParseError(ref, "Cannot parse reference format");
    }

    if (std::holds_alternative<DisplayIdRef>(*parsed)) {
        return resolve_display_id(std::get<DisplayIdRef>(*parsed));
    } else if (std::holds_alternative<AdrRef>(*parsed)) {
        return resolve_adr(std::get<AdrRef>(*parsed));
    } else if (std::holds_alternative<UuidRef>(*parsed)) {
        return resolve_uuid(std::get<UuidRef>(*parsed));
    } else if (std::holds_alternative<PathRef>(*parsed)) {
        return resolve_path(std::get<PathRef>(*parsed));
    }

    throw ParseError(ref, "Unknown reference type");
}

std::vector<BacklogItem> RefResolver::resolve_many(const std::vector<std::string>& refs) const {
    std::vector<BacklogItem> results;
    for (const auto& ref : refs) {
        auto item = resolve_or_none(ref);
        if (item) results.push_back(*item);
    }
    return results;
}

std::optional<BacklogItem> RefResolver::resolve_or_none(const std::string& ref) const {
    try {
        return resolve(ref);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::string> RefResolver::get_references(const BacklogItem& item) {
    std::set<std::string> refs;

    // From links
    for (const auto& r : item.links.relates) refs.insert(r);
    for (const auto& b : item.links.blocks) refs.insert(b);
    for (const auto& nb : item.links.blocked_by) refs.insert(nb);

    // From decisions
    for (const auto& d : item.decisions) refs.insert(d);

    // From body sections
    static const std::regex pattern(R"(\b(?:[A-Z][A-Z0-9]{1,15}-(?:EPIC|FTR|USR|TSK|BUG)-\d{4}|ADR-\d{4}|[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})\b)");
    
    auto extract = [&](const std::optional<std::string>& section) {
        if (!section) return;
        std::smatch match;
        std::string s = *section;
        while (std::regex_search(s, match, pattern)) {
            refs.insert(match.str());
            s = match.suffix().str();
        }
    };

    extract(item.context);
    extract(item.goal);
    extract(item.approach);
    extract(item.acceptance_criteria);
    extract(item.risks);

    std::vector<std::string> result(refs.begin(), refs.end());
    return result;
}

BacklogItem RefResolver::resolve_display_id(const DisplayIdRef& parsed) const {
    std::vector<BacklogItem> matches;
    for (const auto& path : canonical_.list_items()) {
        try {
            auto item = canonical_.read(path);
            if (item.id == parsed.raw) {
                matches.push_back(item);
            }
        } catch (...) {}
    }

    if (matches.size() == 1) {
        return matches.front();
    }

    if (matches.size() > 1) {
        std::vector<std::string> refs;
        refs.reserve(matches.size());
        for (const auto& item : matches) {
            refs.push_back(item.file_path ? item.file_path->string() : item.id);
        }
        throw AmbiguousRefError(parsed.raw, refs);
    }

    throw RefNotFoundError(parsed.raw);
}

BacklogItem RefResolver::resolve_adr(const AdrRef& parsed) const {
    // Resolve ADR-NNNN to KABSD-ADR-NNNN
    std::stringstream ss;
    ss << "KABSD-ADR-" << std::setfill('0') << std::setw(4) << parsed.number;
    std::string target_id = ss.str();

    for (const auto& path : canonical_.list_items()) {
        try {
            auto item = canonical_.read(path);
            if (item.id == target_id) return item;
        } catch (...) {}
    }
    throw RefNotFoundError(parsed.raw);
}

BacklogItem RefResolver::resolve_uuid(const UuidRef& parsed) const {
    for (const auto& path : canonical_.list_items()) {
        try {
            auto item = canonical_.read(path);
            if (item.uid == parsed.uuid) return item;
        } catch (...) {}
    }
    throw RefNotFoundError(parsed.uuid);
}

BacklogItem RefResolver::resolve_path(const PathRef& parsed) const {
    std::filesystem::path path(parsed.path);
    if (!path.is_absolute()) {
        path = std::filesystem::absolute(path);
    }
    return canonical_.read(path);
}

} // namespace kano::backlog_core
