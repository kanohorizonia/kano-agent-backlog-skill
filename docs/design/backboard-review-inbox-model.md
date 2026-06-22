# Backboard Review Inbox Model

Status: accepted data contract for Review Inbox surfacing.

## Goal

Review Inbox surfaces work items that need human attention. It is not a task
planner and it is not an agent dispatcher. Every surfaced item should explain
why it is visible and what human decision is suggested next.

## Canonical Lane Order

| Lane | Purpose | Inclusion | Exclusion | Empty state |
| --- | --- | --- | --- | --- |
| Needs Review | Items that require a human result or evidence decision. | `state=Review`, rejected/needs-fix marker, or explicit review reason. | Closed items with sufficient evidence unless a false-done signal exists. | No items currently need human review. |
| Done Candidate | In-progress items that may be ready for completion review. | `state=InProgress` and validation evidence signal is present. | Items without validation evidence or already in Review/Done. | No in-progress items look ready for completion review. |
| False Done Suspect | Done items whose evidence chain is incomplete. | `state=Done` and evidence summary is incomplete. | Done items with enough durable evidence signals. | No Done items look under-evidenced. |
| Evidence Gap | Reviewable items missing durable evidence. | `state in Ready/Review/Done` and evidence summary is incomplete. | Items outside reviewable states unless another lane explains them. | No reviewable items have obvious evidence gaps. |
| Blocked/Dirty | Items blocked by humans or dirty-work signals. | `state=Blocked` or item text mentions dirty/uncommitted work. | Ordinary Ready/Review items without blocker or dirty-work evidence. | No blocker or dirty-work items are visible. |
| Stale/Drift | Items with freshness or drift risk. | Item id, title, or body mentions stale, drift, outdated, timeout, or timed out. | Fresh items with no drift/freshness terms. | No stale or drift candidates are visible. |
| Ready Frontier | Ready items awaiting approval or dispatch. | `state=Ready`. | Items already in execution, review, blocked, done, or dropped. | No Ready items are waiting at the frontier. |

The service returns this order in `lane_order` and lane definitions in
`lane_taxonomy`. Clients should not infer order by object key enumeration.

## Surfacing Reason Contract

Each Review Inbox bundle includes:

| Field | Meaning |
| --- | --- |
| `review_queue` | Canonical lane name. |
| `reason_code` | Stable machine-readable reason. |
| `review_reason` / `reason_text` | Human-readable explanation. |
| `source_fields` | Item or evidence fields used by the classifier. |
| `confidence` | `high`, `medium`, or `low`; deterministic state/evidence checks are usually high. |
| `diagnostic_status` | `deterministic`, `keyword-signal`, or a future reviewed diagnostic mode. |
| `suggested_human_decision` | Next human action, such as accept, request evidence, reopen, defer, or split follow-up. |

The UI must display at least the human-readable reason and the stable
`reason_code`. Diagnostics should remain visible enough that reviewers can
challenge the classifier.

## Current Reason Codes

| Code | Lane |
| --- | --- |
| `review_state` | Needs Review |
| `rejected_or_needs_fix` | Needs Review |
| `validation_seen_in_progress` | Done Candidate |
| `false_done_evidence_gap` | False Done Suspect |
| `evidence_missing` | Evidence Gap |
| `blocked_state` | Blocked/Dirty |
| `dirty_work_signal` | Blocked/Dirty |
| `stale_or_drift_signal` | Stale/Drift |
| `ready_frontier_candidate` | Ready Frontier |

Reason codes are part of the local API contract. Rename only with a migration
note and test updates.

## Non-Goals

- No automatic state transition.
- No direct agent dispatch.
- No hidden scoring model that cannot explain its source fields.
- No competitor or SaaS comparison copy in Review Inbox messaging.
