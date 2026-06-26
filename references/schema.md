# Backlog Schema

## Directory structure

Backlog roots can carry product hierarchy, topic context, and release evidence as
separate axes:

```
_kano/backlog/
|-- products/<product>/  # Product-scoped hierarchy, artifacts, ADRs, and views
|-- releases/<version>/  # Release records, scope views, evidence, and drafts
`-- topics/              # Horizontal context bundles
```

Release workspaces are defined by the
[Release Record schema](../docs/design/release-record-schema.md) and
[Releases directory contract](../docs/design/releases-directory-contract.md).
They aggregate scope and evidence without changing item parentage.

Product directory structure:

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

- Initiative: independently releasable component, module, or product narrative layer above Epic/Feature.
- Epic: milestone-scale container.
- Feature: capability container and/or release-facing highlight unit under an Initiative or Epic.
- UserStory: user-facing outcome under a Feature or directly under an Epic when no release-facing Feature boundary is useful.
- Task: focused implementation, docs, test, or maintenance work.
- Bug: confirmed defect or regression against existing intent.
- Issue: pre-triage container for an unclear problem, risk, blocker, or runtime gap before it is clear whether follow-up work is a Task, Bug, Feature, or no change.

Research, Decision, Spike, Investigation, Experiment, Validation, Audit,
Migration, Policy Contract, Runbook, Incident, and Deprecation are not formal item
types. Represent that execution shape with the Work Intent metadata fields below,
plus worklogs, ADRs, topics, tags, artifacts, or follow-up Tasks/Bugs/Issues.

`Project` is not a hard formal item type in the current schema. `Initiative` is
the hard top planning item type for independently releasable component narrative
ownership. Product Line / Portfolio grouping above Initiative is catalog
membership, not a parent item. `SubTask` is not a hard item type in this schema;
represent independently delegable subtask-role work as a child Task under a Task,
or keep ordinary steps as a checklist or Worklog note.
See [Canonical backlog taxonomy](../docs/design/canonical-backlog-taxonomy.md)
and [Project model decision](../docs/design/project-model-decision.md).

## Parent rules (canonical design)

- Project-equivalent -> Initiative (stored as root Initiative under product scope)
- Initiative -> Epic, Feature, Bug, or Issue
- Epic -> Feature, UserStory, Task, Bug, or Issue
- Feature -> UserStory, Task, Bug, or Issue
- UserStory -> Task
- Task -> child Task only when it represents independently delegable SubTask-role work
- Bug and Issue may attach to Task, Feature, Epic, Initiative, or Project-equivalent scope; split actionable Issue remediation into Task/Bug follow-ups once triage is clear

Release membership, topics, work orders, and execution dependencies are not
parents. Use release scope, topic membership, work-order context, `blocks`,
`blocked_by`, or `relates` instead. See
[Hierarchy validation matrix](../docs/design/hierarchy-validation-matrix.md) for
the design-level reparent diagnostics.

## Work Intent metadata

Work Intent is optional top-level item metadata. It describes the shape of work
without expanding the hard item-type enum. Use it when a Task, Bug, Issue,
Feature, or other existing item type is being used for no-code investigation,
decision, validation, policy, runbook, incident, migration, deprecation, or other
non-implementation work.

Fields:

- `work_intent`: one of `implementation`, `investigation`, `spike`, `decision`,
  `experiment`, `validation`, `audit`, `migration`, `policy_contract`,
  `runbook`, `incident`, or `deprecation`.
- `execution_mode`: optional compact execution mode, for example `code`,
  `no-code`, `docs-only`, `analysis-only`, or `ops`.
- `result_contract`: optional expected result shape, such as `decision-record`,
  `investigation-notes`, `validation-report`, `runbook-update`, or
  `follow-up-ticket`.
- `evidence_requirement`: optional evidence needed for Review/Done.
- `follow_up_policy`: optional rule for creating implementation/remediation
  follow-up tickets when the result is not code.
- `no_go_or_defer_policy`: optional rule for recording no-change, no-go, defer,
  or blocked outcomes.

Passive provenance extension fields reserved for future multi-human workflows:

- `intent.author`: optional human or agent originator label for the current intent.
- `intent.source`: optional source channel, document, conversation, or import label.
- `intent.owner`: optional current intent steward label; this is not an authority rule.
- `intent.reviewers`: optional list of people or agents asked to review intent.
- `intent.conflicts_with`: optional list of item refs that may conflict with this intent.
- `intent.supersedes`: optional list of item refs whose intent this item supersedes.

These `intent.*` fields are passive metadata only. They do not implement voting,
approval matrices, permission checks, ownership transfer, or automated conflict
resolution. They are safe to omit, and current KOB workflow remains focused on
single-maintainer and small-team review.

`implementation` is the default work intent for newly rendered item templates.
For non-implementation intents, moving from InProgress to Review emits advisory
diagnostics when `result_contract`, `evidence_requirement`, `follow_up_policy`,
or `no_go_or_defer_policy` is missing. These diagnostics do not block the state
transition; they tell reviewers that the no-code result contract is incomplete.

No-code investigation and decision items can be Done when their result contract
is satisfied: the decision or finding is recorded, evidence is linked or quoted,
the follow-up policy says whether implementation tickets were created, and the
no-go/defer policy records why no implementation follows. If implementation is
needed, create or link a separate Task/Bug/Feature follow-up instead of treating
the investigation item as the implementation itself.

## Parent state sync (forward-only)

When a child item state changes, parents can auto-advance forward-only:
- Never downgrade parent state automatically.
- Never change child states based on parent edits.
- Ready/Planned children advance parents to Planned (not Ready).
- Any InProgress/Review/Blocked child advances parent to InProgress.
- All Done => parent Done; all Dropped => parent Dropped; all Duplicate => parent Duplicate; mix Done/Dropped/Duplicate => parent Done.

## Duplicate state

`Duplicate` is a terminal state for a valid item whose canonical ownership lives
on another backlog item. It is distinct from `Dropped`: Dropped means the work is
intentionally abandoned, while Duplicate means the work was already represented
elsewhere and this item should preserve traceability.

When an item moves to `Duplicate`, frontmatter must include `duplicate_of` with
`duplicate_of` and rejects self-references against the item's `id` or `uid`.
List and dashboard views surface `duplicate_of` so reviewers can follow the
canonical target.

## Ready gate (required, non-empty)

To move Tasks, Bugs, or Issues into active execution, each item must include:
- Context
- Goal
- Approach
- Acceptance Criteria
- Risks / Dependencies

Initiatives and Epics require Context and Goal. Features and UserStories require
Context, Goal, and Acceptance Criteria. All items should still carry enough
context for review.

## Done evidence for code-changing items

Record branch convergence evidence in Worklog or Intent Amendments before closing code-changing items:
- `Branch convergence: target=<branch>` or `target_branch=<branch>`; target means the repo default branch unless a human explicitly names another target.
- `implementation_commit=<sha>` and `reachable_from_target=true` or `reachable_from_target=yes`.
- `remote_publication=<remote/ref>` or `remote_publication=true/yes` for the target branch publication evidence.
- `side_branch_delivery=explicit-human-choice` or `side_branch_delivery=human-approved` when a human chooses side-branch-only Done.
- `nested_gitlink=<evidence>` when nested/submodule work needs parent gitlink or submodule pointer evidence.
- `Blocked convergence: branch=<branch>; reason=<reason>; next=<step>; blocker=<owner/item>` when convergence is blocked; the item should remain not Done until resolved.

## File naming

- `<ID>_<slug>.md`
- Slug: ASCII, hyphen-separated
- ID prefixes:
  - `KABSD-INIT-`
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
duplicate_of: null
area: general
iteration: null
work_intent: implementation
execution_mode: null
result_contract: null
evidence_requirement: null
follow_up_policy: null
no_go_or_defer_policy: null
intent.author: null
intent.source: null
intent.owner: null
intent.reviewers: []
intent.conflicts_with: []
intent.supersedes: []
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
