# Maintainer Automation

This page is for OSS maintainers who want a reproducible workflow memory layer around agent assisted development. The repo presents `kano-agent-backlog-skill` as local first, reviewable, and auditable, with markdown artifacts as the durable source of truth.

## Status

This project is pre-1.0. Maintainer automation should favor explicit review, small rollouts, and deterministic artifacts over hidden background behavior.

## What maintainers can automate

Based on the repo docs, maintainers can automate or standardize these workflows:

- backlog initialization for a product
- work item creation and state transitions
- Ready Gate enforcement for tasks, bugs, and issues
- Work Intent metadata for implementation, investigation, decision, validation,
  audit, migration, policy contract, runbook, incident, and deprecation work
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

The Ready Gate is one of the main controls that keeps agent heavy workflows understandable in review. Before execution, the task, bug, or issue should state:

- why the work exists
- what success looks like
- how the team intends to approach it
- how completion will be checked
- what risks or dependencies remain

Schema details belong in [../references/schema.md](../references/schema.md).

Use the formal `Issue` type only for pre-triage unclear problems, risks,
blockers, or runtime gaps. Once triage produces actionable work, link or split it
into Task/Bug follow-ups instead of inventing Research, Decision, Spike,
Investigation, Audit, Migration, Policy, Runbook, Incident, or Deprecation item
types.

## Work Intent for no-code maintainer work

Work Intent is metadata on a normal item, not a hard type. Maintainers can set
`work_intent` to `investigation`, `decision`, `validation`, `audit`, `runbook`,
or another allowed value while keeping `type` as Initiative, Epic, Feature,
UserStory, Task, Bug, or Issue. Non-implementation items should also state:

- `result_contract`: the expected output, such as a decision record or validation report
- `evidence_requirement`: what evidence a reviewer should inspect
- `follow_up_policy`: whether and how implementation/remediation tickets are created
- `no_go_or_defer_policy`: how no-change, no-go, defer, or blocked outcomes are recorded

Optional `intent.author`, `intent.source`, `intent.owner`, `intent.reviewers`,
`intent.conflicts_with`, and `intent.supersedes` fields may preserve provenance
or flag visible intent conflicts for future review. They are not governance
controls: they do not assign authority, require voting, enforce permissions, or
merge conflicting human requests.

For no-code investigations and decisions, Done means the recorded result and
evidence satisfy that contract. If code or operational work follows, create a
linked Task/Bug/Feature rather than stretching the no-code item into an
implementation ticket.

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
