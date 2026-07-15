# Cross-product migration contract

`kob.cross_product_migration.*.v1` defines a dry-run-first, fail-closed contract for moving a canonical item subtree between registered backlog products.

The request identifies the source product and root by display ID or stable UID, the registered target product, the `subtree` scope, optional source-revision and target-prefix guards, and bounded item/artifact limits. A plan records complete UID-preserving item mappings, owned artifacts, supported text-reference rewrites, source and target snapshot revisions, blockers, warnings, affected paths, and a SHA-256 plan hash.

Canonical plan serialization uses UTF-8 JSON with lexicographically ordered object keys and deterministically ordered arrays. `plan_hash` is omitted while hashing; all other plan fields, including revisions, mappings, references, blockers, and warnings, participate in the digest. Apply must recompute preflight and require an exact hash match.

Unknown schema versions, unknown fields, unsupported scopes, missing registrations, incomplete product scaffolds, ambiguous roots, UID/ID collisions, stale revisions, and out-of-bound scans fail closed. Version 1 makes no compatibility promise for unknown future fields. Additive evolution requires a new schema version when it changes hashing or mutation semantics.

Planning is mutation-free: it does not allocate target sequences, create items, write receipts, refresh views, or touch indexes. Result, verification, status, and rollback receipts have separate schemas so an interrupted operation can be resumed or recovered without treating partial state as success.
