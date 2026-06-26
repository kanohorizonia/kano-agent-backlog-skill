# Product Map Projection Schema

Status: draft schema contract for Backboard Product Memory projection.

## Goal

Product Map is a bounded projection of durable backlog and design memory. It is
not a replacement hierarchy, not an execution scheduler, and not a graph runtime.
Backboard can use the projection to show product shape, review context, vision
scope, and gaps without mutating backlog items or starting agents.

## Projection Scope

The projection contains product-scoped nodes and typed references:

| Node type | Purpose |
| --- | --- |
| `vision` | Vision-layer record for strategic intent above Initiative and Epic. |
| `initiative` | Independently releasable component, module, or product narrative line. |
| `epic` | Long-running capability or outcome group. |
| `feature` | Reviewable product capability or work stream. |
| `topic` | Curated context bundle. |
| `work_order` | Backlog item that can receive evidence and review. |
| `adr` | Durable design decision record. |
| `evidence` | Validation, artifact, report, or review proof. |

Node refs must be stable repo-visible ids such as `backlog_id`, `product`,
`vision_id`, `item_id`, `uid`, `topic_id`, `adr_id`, `goal_id`, `release_id`, and
`evidence_id`. Raw filesystem paths are optional diagnostics only and must not be
required for graph identity. Cross-backlog refs use logical `backlog_id` and
`product` values, not local checkout paths.

## Vision-Layer Rules

Product Map may render the vision-layer model from
[Vision-Layer Management Model](vision-layer-management-model.md):

- a `vision` node is strategic scope, not a hard work-item type;
- vision edges may point to Initiatives, Epics, Features, Topics, roadmap goals,
  release records, ADRs, and evidence;
- Topic edges are horizontal context associations and must not be rendered as
  parent hierarchy;
- Product Line / Portfolio grouping remains catalog membership over Initiatives,
  not `parent` or Product Map ownership;
- missing legacy roots, orphan Epics, stale links, blocked areas, and dogfood
  evidence gaps must be shown as diagnostics rather than inferred history.

## Diagnostics

Product Map diagnostics are explicit data, not hidden assumptions:

| Code | Meaning | Suggested review action |
| --- | --- | --- |
| `missing_ref` | A referenced node cannot be resolved in the active product. | Fix the source reference or mark the gap known. |
| `stale_ref` | A referenced node exists but its source evidence is older than the consumer expects. | Revalidate or acknowledge staleness. |
| `ambiguous_ref` | More than one candidate matches a non-stable reference. | Replace with stable id or uid. |
| `evidence_gap` | A node claims a state without enough durable evidence refs. | Request evidence before trusting the projection. |
| `orphan_epic` | An Epic lacks Initiative or vision coverage during migration. | Link to an Initiative or record why product projection is still valid. |
| `blocked_area` | A vision scope contains blocked or at-risk work. | Review blockers before treating the roadmap slice as healthy. |
| `vision_scope_gap` | A strategic scope lacks required product, backlog, topic, or dogfood coverage. | Add refs or record the gap as intentional. |

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
