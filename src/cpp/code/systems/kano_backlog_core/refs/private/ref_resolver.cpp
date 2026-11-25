#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <regex>
#include <set>
#include <iomanip>
#include <sstream>
#include <utility>

namespace kano::backlog_core {

RefResolver::RefResolver(const CanonicalStore& canonical) : canonical_(canonical) {}

BacklogItem RefResolver::resolve(const std::string& ref) const {
    diagnostics::ScopedMutationSpan span("ref_resolver.resolve", ref);
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

    static const std::regex canonical_prose_pattern(
        R"(\b(?:[A-Z][A-Z0-9]{1,15}-(?:INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4,}|ADR-\d{4})\b)"
    );

    static const std::regex remapped_id_message_pattern(
        R"(^Remapped ID: ([A-Z][A-Z0-9]{1,15}-(?:INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4,}) -> ([A-Z][A-Z0-9]{1,15}-(?:INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-[0-9]{4,})$)"
    );

    const auto historical_prose_ids = [&]() {
        std::vector<std::pair<std::string, std::string>> remap_chain;
        for (const auto& raw_line : item.worklog) {
            auto line = raw_line;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const auto parsed_entry = WorklogEntry::parse(line);
            if (!parsed_entry) {
                if (line.find("Remapped ID:") != std::string::npos) {
                    return std::set<std::string>{};
                }
                continue;
            }

            std::smatch match;
            if (!std::regex_match(parsed_entry->message, match, remapped_id_message_pattern)) {
                if (parsed_entry->message.find("Remapped ID:") != std::string::npos) {
                    return std::set<std::string>{};
                }
                continue;
            }
            remap_chain.emplace_back(match[1].str(), match[2].str());
        }

        if (remap_chain.empty()) {
            return std::set<std::string>{};
        }

        std::set<std::string> visited_ids;
        std::set<std::string> historical_ids;
        std::string cursor = remap_chain.front().first;
        visited_ids.insert(cursor);
        for (const auto& [old_id, new_id] : remap_chain) {
            if (old_id != cursor || old_id == new_id || visited_ids.contains(new_id)) {
                return std::set<std::string>{};
            }
            historical_ids.insert(old_id);
            visited_ids.insert(new_id);
            cursor = new_id;
        }

        if (cursor != item.id) {
            return std::set<std::string>{};
        }
        historical_ids.erase(item.id);
        return historical_ids;
    }();

    const auto extract_canonical_prose_tokens = [&](const std::string& text) {
        std::smatch match;
        std::string remaining = text;
        while (std::regex_search(remaining, match, canonical_prose_pattern)) {
            if (!historical_prose_ids.contains(match.str())) {
                refs.insert(match.str());
            }
            remaining = match.suffix().str();
        }
    };

    // From links
    for (const auto& r : item.links.relates) refs.insert(r);
    for (const auto& b : item.links.blocks) refs.insert(b);
    for (const auto& nb : item.links.blocked_by) refs.insert(nb);

    // Decisions are frequently provenance prose rather than a structured ref.
    // Validate canonical tokens inside that prose without treating the entire
    // sentence (which may contain slashes) as a path reference. UUIDv7 values
    // in prose are external evidence identifiers unless explicitly stored in a
    // structured link field.
    for (const auto& decision : item.decisions) {
        extract_canonical_prose_tokens(decision);
    }

    // From body sections
    auto extract = [&](const std::optional<std::string>& section) {
        if (!section) return;
        extract_canonical_prose_tokens(*section);
    };

    extract(item.context);
    extract(item.goal);
    extract(item.non_goals);
    extract(item.intent_amendments);
    extract(item.approach);
    extract(item.acceptance_criteria);
    extract(item.risks);

    std::vector<std::string> result(refs.begin(), refs.end());
    return result;
}

BacklogItem RefResolver::resolve_display_id(const DisplayIdRef& parsed) const {
    diagnostics::ScopedMutationSpan span("ref_resolver.resolve_display_id", parsed.raw);
    if (auto exact_path = canonical_.find_item_path_by_id(parsed.raw)) {
        auto item = canonical_.read(*exact_path);
        if (item.id == parsed.raw) {
            return item;
        }
    }

    diagnostics::ScopedMutationSpan fallback_span("ref_resolver.resolve_display_id.scan", parsed.raw);
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
    diagnostics::ScopedMutationSpan span("ref_resolver.resolve_adr", parsed.raw);
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
    diagnostics::ScopedMutationSpan span("ref_resolver.resolve_uuid.scan", parsed.uuid);
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
