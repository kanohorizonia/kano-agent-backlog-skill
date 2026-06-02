# Maintainer Automation

This page is for OSS maintainers who want a reproducible workflow memory layer around agent assisted development. The repo presents `kano-agent-backlog-skill` as local first, reviewable, and auditable, with markdown artifacts as the durable source of truth.

## Status

This project is pre-1.0. Maintainer automation should favor explicit review, small rollouts, and deterministic artifacts over hidden background behavior.

## What maintainers can automate

Based on the repo docs, maintainers can automate or standardize these workflows:

- backlog initialization for a product
- work item creation and state transitions
- Ready Gate enforcement for tasks and bugs
- append only worklog entries
- ADR creation and linkage
- workset setup, refresh, promotion, and cleanup
- topic creation, pinning, distillation, and export
- view generation and refresh
- optional indexing and search pipelines
- doctor and validation style checks

## Recommended maintainer workflow

1. initialize the backlog in the target repository
2. document how contributors should choose item types
3. require Ready Gate completion before active work
4. require worklog updates for direction changes and state changes
5. use ADRs when decisions affect architecture or long term tradeoffs
6. use worksets for focused execution and topics for multi item context
7. treat search and embedding layers as optional derived aids

Detailed process expectations live in [../references/workflow.md](../references/workflow.md).

## What stays canonical

Canonical, reviewable artifacts are the markdown files under the backlog structure, including:

- work items
- worklogs
- ADRs
- generated views that your team decides to keep
- stable topic briefs that humans maintain

Worksets, cache directories, logs, and some topic materials are derived data and should be treated accordingly.

## Ready Gate for maintainers

The Ready Gate is one of the main controls that keeps agent heavy workflows understandable in review. Before execution, the task or bug should state:

- why the work exists
- what success looks like
- how the team intends to approach it
- how completion will be checked
- what risks or dependencies remain

Schema details belong in [../references/schema.md](../references/schema.md).

## Topics and worksets serve different roles

- use **worksets** to keep one active task from drifting during execution
- use **topics** to hold multi item context, pinned docs, snippets, and deterministic briefs across sessions or handoffs

That split is already documented in [workset.md](workset.md) and [topic.md](topic.md).

## Optional experimental search

The repo includes optional SQLite and embedding oriented indexing references. These can improve retrieval for large backlogs, but they are not the core value proposition.

For OSS readiness, present them as optional and experimental. The primary promise remains local markdown artifacts that are easy to inspect and review.

## Maintainer checklist

- keep `_kano/backlog/` human readable
- keep `.kano/cache` and shared logs out of version control
- prefer `kano-backlog` driven operations in maintainer docs and release examples so the published CLI surface stays coherent
- review generated artifacts before treating them as durable project memory
- avoid writing docs that imply a stable API surface where the repo still says experimental

## Related

- [codex-for-oss.md](codex-for-oss.md)
- [demo-maintenance.md](demo-maintenance.md)
- [../references/workflow.md](../references/workflow.md)
