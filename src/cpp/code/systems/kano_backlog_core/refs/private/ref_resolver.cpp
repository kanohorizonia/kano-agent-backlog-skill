#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <regex>
#include <set>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

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
        R"(\b(?:[A-Z][A-Z0-9]{1,15}-(?:INIT|EPIC|FTR|USR|TSK|SUBTSK|BUG|ISS)-\d{4}|ADR-\d{4})\b)"
    );

    const auto is_explicit_historical_source = [](const std::string& text, std::size_t token_start) {
        auto lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const auto remap_start = lower.rfind("canonical remap:", token_start);
        if (remap_start != std::string::npos) {
            const auto represented_by = lower.find("represented by", remap_start);
            const auto clause_end = lower.find_first_of(".\r\n", remap_start);
            if (represented_by != std::string::npos &&
                (clause_end == std::string::npos || represented_by < clause_end) &&
                token_start < represented_by) {
                return true;
            }
        }

        const auto context_start = token_start > 32 ? token_start - 32 : 0;
        const auto prefix = lower.substr(context_start, token_start - context_start);
        return prefix.ends_with("legacy ") || prefix.ends_with("mislabeled ");
    };

    const auto extract_canonical_prose_tokens = [&](const std::string& text) {
        std::smatch match;
        std::string remaining = text;
        std::size_t consumed = 0;
        while (std::regex_search(remaining, match, canonical_prose_pattern)) {
            const auto token_start = consumed + static_cast<std::size_t>(match.position());
            if (!is_explicit_historical_source(text, token_start)) {
                refs.insert(match.str());
            }
            consumed = token_start + static_cast<std::size_t>(match.length());
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
