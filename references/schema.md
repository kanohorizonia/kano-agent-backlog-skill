# Backlog Schema

## Directory structure

```
_kano/backlog/products/<product>/
├── _config/          # Product configuration (config.toml, profile.env)
├── _meta/            # Schema, conventions, and metadata
├── _trash/           # Archived/deleted items (timestamped)
├── .cache/           # Generated indexes (SQLite, embeddings)
├── artifacts/        # Work outputs organized by item ID
│   └── <item-id>/    # Demo reports, analysis, diagrams, test results
├── decisions/        # ADR decision records
├── items/            # Backlog items organized by type and bucket
│   ├── epic/
│   ├── feature/
│   ├── userstory/
│   ├── task/
│   ├── bug/
│   └── issue/
└── views/            # Generated dashboards and reports
```

## Process-defined types and states

Item types and states come from the active process profile. See
`references/processes.md` and the profile selected via
`_kano/backlog/_config/config.json` (`process.profile` or `process.path`).

When scripts or docs need workflow details, they should load the profile
specified in config (built-in or custom) instead of hardcoding the list.

## Formal item types

- Epic: milestone-scale container.
- Feature: capability container under an Epic.
- UserStory: user-facing outcome under a Feature.
- Task: focused implementation, docs, test, or maintenance work.
- Bug: confirmed defect or regression against existing intent.
- Issue: pre-triage container for an unclear problem, risk, blocker, or runtime gap before it is clear whether follow-up work is a Task, Bug, Feature, or no change.

Research, Decision, Spike, and Investigation are not formal item types. Treat them as activity metadata in worklogs, ADRs, topics, tags, artifacts, or follow-up Tasks/Bugs/Issues.

## Parent rules (default)

- Epic -> Feature
- Feature -> UserStory
- UserStory -> Task or Bug
- Feature -> Bug (allowed)
- Task -> Task (optional sub-task)
- Issue may be standalone, linked through `links.*`, or parented under a Feature/UserStory when the scope is already known; split actionable remediation into Task/Bug follow-ups once triage is clear.
- Epic has no parent

These defaults align with built-in profiles; custom processes may define
different parent relationships.

## Parent state sync (forward-only)

When a child item state changes, parents can auto-advance forward-only:
- Never downgrade parent state automatically.
- Never change child states based on parent edits.
- Ready/Planned children advance parents to Planned (not Ready).
- Any InProgress/Review/Blocked child advances parent to InProgress.
- All Done => parent Done; all Dropped => parent Dropped; mix Done/Dropped => parent Done.

## Ready gate (required, non-empty)

To move Tasks, Bugs, or Issues into active execution, each item must include:
- Context
- Goal
- Approach
- Acceptance Criteria
- Risks / Dependencies

Epics and Features use the lighter profile-specific gates in validation, but should still carry enough context for review.

## File naming

- `<ID>_<slug>.md`
- Slug: ASCII, hyphen-separated
- ID prefixes:
  - `KABSD-EPIC-`
  - `KABSD-FTR-`
  - `KABSD-USR-`
  - `KABSD-TSK-`
  - `KABSD-BUG-`
  - `KABSD-ISS-`
- Prefix derivation:
  - Source: `config/profile.env` -> `PROJECT_NAME`.
  - Split on non-alphanumeric separators and camel-case boundaries, take first letters.
  - If only one letter, use the first letter plus the next consonant (A/E/I/O/U skipped).
  - If still short, use the first two letters.
  - Uppercase the result (example: `kano-agent-backlog-skill-demo` -> `KABSD`).
- Store files under `_kano/backlog/items/<type>/<bucket>/` by item type.
- Bucket names use the lower bound of each 100 range:
  - `0000`, `0100`, `0200`, ...
- For Epic, create `<ID>_<slug>.index.md` in the same folder.

## Frontmatter (minimum)

```
---
id: KABSD-TSK-0001
type: Task
title: "Short title"
state: Proposed
priority: P2
parent: KABSD-USR-0001
area: general
iteration: null
tags: []
created: 2026-01-02
updated: 2026-01-02
owner: null
external:
  azure_id: null
  jira_key: null
links:
  relates: []
  blocks: []
  blocked_by: []
decisions: []
---
```

## Immutable fields

- `id`, `type`, `created` must not be changed after creation.

## Config defaults (baseline)

Baseline config lives at `_kano/backlog/_config/config.json` and defaults to:

```
{
  "log": { "verbosity": "info", "debug": false },
  "process": { "profile": "builtin/azure-boards-agile", "path": null },
  "sandbox": { "root": "_kano/backlog_sandbox" },
  "index": { "enabled": false, "backend": "sqlite", "path": null, "mode": "rebuild" }
}
```

## Config overrides (environment)

- `KANO_BACKLOG_CONFIG_PATH`: override config file path (must be under `_kano/backlog` or `_kano/backlog_sandbox`).
- Audit log env overrides (highest precedence over config defaults):
  - `KANO_AUDIT_LOG_DISABLED`
  - `KANO_AUDIT_LOG_ROOT`
  - `KANO_AUDIT_LOG_FILE`
  - `KANO_AUDIT_LOG_MAX_BYTES`
  - `KANO_AUDIT_LOG_MAX_FILES`

## Parent reference format (collision-safe)

- Same-product parent: use the display ID in `parent` (e.g., `KABSD-FTR-0002`).
- Parent is intentionally **intra-product**: it powers hierarchy and parent state sync; cross-product “parent” relationships are usually not desired.
- Cross-product relationships should be expressed via `links.relates`, `links.blocks`, `links.blocked_by` instead of `parent`.
  - For collision-safe references in links/content, use `id@uidshort` (e.g., `KABSD-FTR-0002@019b8f52`) or full `uid` (`019b8f52-9fde-7162-bd19-e9b8310526fc`).

### Validation

- The `workitem_update_state.py` script validates that `parent` resolves uniquely within the current product.
- If unresolved or ambiguous, the script aborts with guidance to keep parent intra-product and use `links.*` (with `id@uidshort` / full `uid`) for cross-product relationships.

### Rationale

- Display IDs are human-friendly and stable for day-to-day work within a product.
- Parent drives hierarchy and state propagation; limiting it to a product keeps behavior predictable.
- Cross-product collisions are expected in a multi-product platform; disambiguated refs in links ensure correctness without sacrificing readability in the common case.

