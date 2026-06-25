# Version Goal Ledger Schema

Status: draft schema contract for Backboard roadmap projections.

## Goal

Version goals are falsifiable roadmap targets. They are not epics, features,
topics, or work orders, and they must not become another hierarchy tree.
Backboard can project version goals over existing records to show what is done,
what is unverified, and what has been cut or deferred.

## Goal Fields

Each ledger entry includes:

| Field | Meaning |
| --- | --- |
| `goal_id` | Stable id for the roadmap goal. |
| `target_version` | Version or release line the goal targets. |
| `status` | Roadmap status from the taxonomy below. |
| `evidence_quality` | Evidence quality summary for the goal. |
| `linked_refs` | Epics, features, tasks, topics, ADRs, or evidence refs. |
| `dogfood_coverage` | Whether internal dogfood use exists and where it is recorded. |
| `gap_state` | Known missing evidence, scope, or implementation gaps. |
| `rationale` | Required when the goal is cut, deferred, partial, or at risk. |

## Roadmap Goal Status Taxonomy

| Status | Meaning |
| --- | --- |
| `Done` | Implementation and evidence are accepted for the goal. |
| `Implemented/Unverified` | Code or docs exist but evidence is not sufficient. |
| `Partial` | A bounded subset is complete; remaining scope is explicit. |
| `Deferred` | Goal remains valid but is moved out of this target version. |
| `Cut` | Goal is intentionally removed from the target version. |
| `Blocked` | External or internal blocker prevents completion. |
| `At Risk` | Current evidence suggests the goal may miss the target. |
| `Unknown` | Backboard cannot compute status from durable evidence. |

## Rules

- `Done` requires evidence refs and must not be inferred from closed tickets
  alone.
- `Implemented/Unverified` is the correct state for code without validation.
- `Unknown` is safer than inventing unsupported roadmap history.
- Backboard may show roadmap status, but must not mutate backlog or start agents.
- Ark Console execution admission is separate and out of scope.

## Release Record Integration

Release records may reference Version Goal Ledger goals when a release needs a
bounded snapshot of roadmap intent. The Release Record does not replace this
ledger, does not own roadmap status, and does not turn release membership into a
parent relationship. See [Release Record Schema](release-record-schema.md) and
[Releases Directory Contract](releases-directory-contract.md).

## Fixture

The schema and example fixture live in:

- [version-goal-ledger.schema.json](../../references/version-goal-ledger.schema.json)
- [version-goal-ledger.fixture.json](../../references/version-goal-ledger.fixture.json)
