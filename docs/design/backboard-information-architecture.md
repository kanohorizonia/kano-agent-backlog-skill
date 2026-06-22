# Backboard Information Architecture

Status: accepted design contract for the local review UI.

## Naming

Backboard is the product-facing name for the local backlog review surface.
`KOB Webview` remains the implementation name for the native Drogon service,
binary, scripts, and technical docs.

Use Backboard in user-facing navigation, screenshots, and public docs. Use KOB
Webview only when describing the native process, routes, ports, Docker image, or
developer commands.

## First Screen

Backboard opens to Review Inbox by default. The first screen should answer:

- what needs human attention
- why each item is surfaced
- which product and state filters are active
- whether the UI is loading, stale, failed, or ready

Backboard must not start with a marketing page or a broad project dashboard. It
is a work surface for repeated review.

## Primary Navigation

Primary navigation is reader-facing and task-oriented:

| Navigation | Purpose |
| --- | --- |
| Review Inbox | Default review queue for human decisions. |
| Product Map | Hierarchical backlog map for themes, epics, features, stories, and tasks. |
| Flow | Kanban-style state scan. |
| Context | ADR, topic, and workset context. |
| Dependencies | Link and dependency graph. |
| Agent Runs | Work-order timeline and agent execution evidence. |
| Command | Read-only command palette preview. |

The navigation may grow, but new entries must be durable review surfaces rather
than shortcuts to one-off diagnostics.

## Item Detail Layout

Item detail should present the durable intent contract before raw text:

1. Summary: id, title, product, type, state, priority, owner, reviewer.
2. Ready fields: Context, Goal, Approach, Acceptance Criteria, Risks.
3. Evidence: validation matrix, artifacts, missing evidence, confidence notes.
4. Timeline: worklog events and agent run evidence.
5. Links: parent, children, dependency edges, ADRs, topics, worksets.
6. Raw Markdown: source file body for exact review.

The detail view must keep source paths and raw Markdown reachable because the
repository remains the source of truth.

## Cross-Linking Rules

- Item links include both product and item id when possible.
- Deep links should preserve active product, state, type, search, and limit
  filters.
- Missing or invalid links are visible diagnostics, not silent omissions.
- Graph links must distinguish semantic edge kinds such as parent, blocks,
  blocked_by, relates, topic-membership, and missing-node.

## Execution Boundary

Backboard is read-only for this milestone. It may explain suggested human
decisions, but it must not dispatch agents, mutate items, mark items Done, or
proxy private execution controls.

Any future mutation or execution control requires a separate design contract,
explicit review gate, audit trail, and tests.
