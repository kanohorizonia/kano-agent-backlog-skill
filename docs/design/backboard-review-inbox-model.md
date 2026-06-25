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

Detector output is advisory only. `suggested_human_decision` and
`suggested_decision` describe the classifier's recommendation; they are not a
submitted human decision and must not mutate KOB state by themselves.

## Human Review Decision Records

Backboard review actions create KOB-native `review_decision` JSON records under
the product `_meta/review-decisions/` tree.

Draft records live under `_meta/review-decisions/drafts/` and may be edited
before submission. Saving a draft updates the same draft file for the same item
and actor alias.

Submitted records live under `_meta/review-decisions/submitted/<item-id>/`.
Submitted records are append-only: later changes create a new submitted record
with `supersedes` pointing at the earlier record instead of rewriting it.

Each submitted record includes:

| Field | Meaning |
| --- | --- |
| `lane` | Review Inbox lane where the decision was made. |
| `reason_code` | Stable machine-readable detector reason. |
| `suggested_decision` | Detector suggestion copied for audit context only. |
| `human_decision` | Human-selected lane-specific action. |
| `rationale` | Human rationale or instruction text. |
| `actor_alias` | Human or operator alias submitting the decision. |
| `target_state` | Optional KOB target state requested by the action. |
| `created_at` / `submitted_at` | UTC decision timestamps. |
| `source_detector` | Detector or UI source that surfaced the lane. |
| `supersedes` | Optional prior submitted decision record path. |
| `transition` | Result of the KOB policy transition when `target_state` is requested. |

Lane actions use explicit names such as `Send To Review`, `Request More
Evidence`, `Approve Done With Evidence`, `Accept Evidence Risk`, and `Reopen
Done For Review` instead of generic Accept/Reject labels where possible.

High-risk actions, including moving to Done, dropping work, accepting evidence
risk, or reopening Done, require explicit confirmation before any KOB state
transition is attempted. Confirmed actions call the existing KOB transition
policy; Backboard must not bypass transition validation and must not start
agents or dispatch work.

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
