# Relation Operations

KOB owns canonical relation mutation and bounded relation queries for `relates`, `blocks`, and `blocked_by`.

## Canonical semantics

- `A blocks B` is the execution edge `A -> B`.
- `B blocked_by A` is the same execution edge `A -> B`.
- `relates` is non-directional for duplicate detection and does not participate in dependency-cycle checks.
- Only one source item stores a relation. Incoming and inverse views are derived from configured products, so mutation never requires a two-item transaction.
- Cross-product frontmatter stores the target canonical display ID. Product identity is resolved from the configured, collision-free display-ID prefix.

Dependency cycles are checked across all configured products within explicit product and item scan limits. Hierarchy links and `relates` are excluded from that graph.

## CLI

All mutations are dry-run by default. Pass `--apply` only after reviewing the JSON plan.

```powershell
kano-backlog -p C:\workspace relation add KOB-FTR-0036 KOA-TSK-0364 `
  --source-product kano-agent-backlog-skill `
  --target-product kano-agent-ark-skill `
  --type relates `
  --agent codex `
  --idempotency-key relation-example-v1 `
  --format json

kano-backlog -p C:\workspace relation remove KOB-FTR-0036 KOA-TSK-0364 `
  --source-product kano-agent-backlog-skill `
  --target-product kano-agent-ark-skill `
  --type relates `
  --agent codex `
  --apply `
  --format json

kano-backlog -p C:\workspace relation list KOB-FTR-0036 `
  --product kano-agent-backlog-skill `
  --type relates `
  --direction both `
  --limit 100 `
  --format json
```

Product arguments accept a configured canonical product name or an unambiguous configured prefix. Path-like product and item inputs are rejected. Item arguments accept canonical display IDs or UUIDv7 UIDs; arbitrary file paths are not supported.

## Retry and write evidence

Add and remove are state-idempotent. A repeated add returns `already_present`; a repeated remove returns `already_absent`; neither appends another worklog entry. `idempotency_key` is recorded on an actual mutation for correlation, while canonical state remains the source of truth for retry decisions.

Confirmed responses expose `applied`, `worklog_appended`, `index_refreshed`, and `read_after_write`. The operation re-resolves both endpoints and rechecks the complete bounded relation graph immediately before writing. A single canonical owner is updated, re-indexed, and read back. Removal fails closed when duplicate legacy owners would require a non-atomic multi-file cleanup.

## Bounds and pagination

- Product scan default: 64.
- Item scan default: 20,000.
- List page maximum: 500.
- List cursors use the opaque `relation-v1` contract and are valid only for the same stable query inputs.

Callers should budget for two bounded catalog scans on confirmed mutations: one for planning and one immediately before write. Dry-run and list perform one scan.
