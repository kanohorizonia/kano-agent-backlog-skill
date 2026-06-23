# Product Map Projection Schema

Status: draft schema contract for Backboard Product Memory projection.

## Goal

Product Map is a bounded projection of durable backlog and design memory. It is
not a replacement hierarchy, not an execution scheduler, and not a graph runtime.
Backboard can use the projection to show product shape, review context, and gaps
without mutating backlog items or starting agents.

## Projection Scope

The projection contains product-scoped nodes and typed references:

| Node type | Purpose |
| --- | --- |
| `vision` | Product or roadmap intent statement. |
| `epic` | Long-running capability or outcome group. |
| `feature` | Reviewable product capability or work stream. |
| `topic` | Curated context bundle. |
| `work_order` | Backlog item that can receive evidence and review. |
| `adr` | Durable design decision record. |
| `evidence` | Validation, artifact, report, or review proof. |

Node refs must be stable repo-visible ids such as `product`, `item_id`, `uid`,
`topic_id`, `adr_id`, and `evidence_id`. Raw filesystem paths are optional
diagnostics only and must not be required for graph identity.

## Diagnostics

Product Map diagnostics are explicit data, not hidden assumptions:

| Code | Meaning | Suggested review action |
| --- | --- | --- |
| `missing_ref` | A referenced node cannot be resolved in the active product. | Fix the source reference or mark the gap known. |
| `stale_ref` | A referenced node exists but its source evidence is older than the consumer expects. | Revalidate or acknowledge staleness. |
| `ambiguous_ref` | More than one candidate matches a non-stable reference. | Replace with stable id or uid. |
| `evidence_gap` | A node claims a state without enough durable evidence refs. | Request evidence before trusting the projection. |

## Backboard Rules

- Backboard may render Product Map nodes, refs, and diagnostics.
- Backboard must keep Review Inbox as the default homepage.
- Backboard must not treat Product Map edges as permission to mutate backlog.
- Ark Console execution admission is separate and out of scope.
- Unsupported history must appear as an evidence gap, not as inferred fact.

## Fixture

The schema and example fixture live in:

- [product-map-projection.schema.json](../../references/product-map-projection.schema.json)
- [product-map-projection.fixture.json](../../references/product-map-projection.fixture.json)
