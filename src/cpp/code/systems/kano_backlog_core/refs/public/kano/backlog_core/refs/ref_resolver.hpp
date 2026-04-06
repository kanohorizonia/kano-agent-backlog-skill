#pragma once

#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/refs/ref_parser.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace kano::backlog_core {

class RefResolver {
public:
    explicit RefResolver(const CanonicalStore& canonical);

    /**
     * Resolve a reference string to a BacklogItem.
     * Throws RefNotFoundError or ParseError.
     */
    BacklogItem resolve(const std::string& ref) const;

    /**
     * Resolve multiple references, skipping unresolvable ones.
     */
    std::vector<BacklogItem> resolve_many(const std::vector<std::string>& refs) const;

    /**
     * Resolve reference, returning nullopt if not found or invalid.
     */
    std::optional<BacklogItem> resolve_or_none(const std::string& ref) const;

    /**
     * Extract all references from an item (links, decisions, body).
     */
    static std::vector<std::string> get_references(const BacklogItem& item);

private:
    const CanonicalStore& canonical_;

    BacklogItem resolve_display_id(const DisplayIdRef& parsed) const;
    BacklogItem resolve_adr(const AdrRef& parsed) const;
    BacklogItem resolve_uuid(const UuidRef& parsed) const;
    BacklogItem resolve_path(const PathRef& parsed) const;
};

} // namespace kano::backlog_core
