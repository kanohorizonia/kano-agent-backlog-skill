# Cross-product migration contract

`kob.cross_product_migration.*.v1` defines a dry-run-first, fail-closed contract for moving a canonical item subtree between registered backlog products.

The request identifies the source product and root by display ID or stable UID, the registered target product, the `subtree` scope, optional source-revision and target-prefix guards, and explicit source inventory, target inventory, subtree item, owned artifact, artifact byte, mutation materialization, reference-file, aggregate reference-byte, and reference-record limits. A plan records complete UID-preserving item mappings, owned artifacts, supported text-reference rewrites, source and target snapshot revisions, blockers, warnings, affected paths, and a SHA-256 plan hash.

Canonical plan serialization uses UTF-8 JSON with lexicographically ordered object keys and deterministically ordered arrays. `plan_hash` is omitted while hashing; all other plan fields, including revisions, mappings, references, blockers, and warnings, participate in the digest. A ready plan normalizes its resolved source revision and target prefix into `request.expected_source_revision` and `request.expected_target_prefix` before hashing. Apply may submit those returned guards without changing the reviewed hash, must recompute preflight, and requires an exact hash match.

Unknown schema versions, unknown fields, unsupported scopes, missing registrations, incomplete product scaffolds, ambiguous roots, UID/ID collisions, stale revisions, and out-of-bound scans fail closed. Version 1 makes no compatibility promise for unknown future fields. Additive evolution requires a new schema version when it changes hashing or mutation semantics.

Reference discovery streams eligible product entries, sorts the complete bounded file set into deterministic path order, tokenizes each bounded file once, and performs constant-time source-ID lookup rather than rescanning every file for every migrated item. `max_reference_files`, `max_reference_bytes`, and `max_references` independently cap file enumeration, aggregate scan bytes, and emitted rewrite records. Directory traversal also fails closed after `4 * max_reference_files + 64` visited entries so empty or adversarial directory trees cannot bypass the file bound, and symlinked roots or entries are rejected. Individual files remain capped at 16 MiB. All request bounds participate in the plan hash, are persisted in the transaction journal, and are reused by post-apply verification only after the embedded plan hash is recomputed. Exceeding either file or entry enumeration ceiling produces the same canonical enumeration-limit diagnostic, so blocked receipts remain traversal-order independent.

Canonical source and target inventory scans use `max_source_inventory_items` and `max_target_inventory_items` before parsing item metadata. Inventory file sizes are checked against the 16 MiB per-file reference ceiling and the aggregate `max_reference_bytes` budget before metadata parsing. Owned artifact traversal uses `max_artifacts` as its file-count ceiling and the same derived entry ceiling, while `max_artifact_bytes` caps aggregate artifact reads; artifact, snapshot, retained-output, and staged-file hashes stream through fixed-size buffers, and reference-only `.cache`, `views`, and `.git` exclusions do not hide owned artifacts. Target display-ID allocation fails closed before the signed canonical number range can overflow. Apply reuses the exact hashed request, bounds any derived-index inventory rebuild, applies the same per-file and aggregate byte ceilings before bootstrap item reads, prechecks an existing index against remaining capacity, and caps the aggregate in-memory mutation outputs retained for staging with `max_materialization_bytes` without duplicating retained output buffers. These guards run before canonical publication; exceeding them fails closed with no partial migration success.

Planning is mutation-free: it does not allocate target sequences, create items, write receipts, refresh views, or touch indexes. Result, verification, status, and rollback receipts have separate schemas so an interrupted operation can be resumed or recovered without treating partial state as success.

## Apply and recovery lifecycle

`migration apply` requires `--confirm` and the exact `plan_hash` returned by a fresh ready plan. Apply recomputes the complete preflight, rejects source or target drift, stages every output, verifies staged hashes, publishes the complete target subtree and owned artifacts, rewrites bounded canonical and external references, persists an old-ID alias map, retires source paths, updates the shared derived index and target sequences, and verifies postconditions. The source UID and frontmatter timestamps are preserved while display IDs and mapped references are rewritten.

The transaction journal lives under `.cache/migrations/<plan_hash>/` and contains exact before/after hashes plus private backups. The public alias receipt lives under the target product at `_meta/migrations/<plan_hash>.json`. Old display IDs are not left as active canonical items; they resolve only through this migration metadata. Unsupported reference classes, concurrent SQLite WAL state, changed files, missing backups, and unknown journal schemas fail closed.

An exception after staging, target publication, external-reference rewrite, or source retirement triggers automatic rollback. `migration verify` checks journal hashes, target IDs and preserved UIDs, source retirement, alias presence, reference closure, and index ID/UID paths. Repeating a verified apply returns `already_applied`. `migration rollback --confirm` restores exact pre-migration bytes and is itself idempotent; it refuses to overwrite post-apply drift.

## CLI

```text
kano-backlog migration plan <source_ref> --source-product <slug> --target-product <slug> [--max-source-inventory-items <count>] [--max-target-inventory-items <count>] [--max-artifacts <count>] [--max-artifact-bytes <bytes>] [--max-materialization-bytes <bytes>] [--max-reference-files <count>] [--max-reference-bytes <bytes>] [--max-references <count>] [other guards and bounds]
kano-backlog migration apply <source_ref> --source-product <slug> --target-product <slug> --plan-hash <sha256> --confirm [same guards and bounds]
kano-backlog migration verify <plan_hash> [--backlog-root <path>]
kano-backlog migration status <plan_hash> [--backlog-root <path>]
kano-backlog migration rollback <plan_hash> --confirm [--backlog-root <path>]
```
