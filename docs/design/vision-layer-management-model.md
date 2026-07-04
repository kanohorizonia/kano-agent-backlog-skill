# Vision-Layer Management Model

Status: accepted design contract for `KOB-FTR-0023`.

## Decision

KOB uses the term **vision-layer record** for strategic planning scope above
Initiative and Epic. A vision-layer record is the selected Project-equivalent
model from [Project Model Decision](project-model-decision.md): it is not a hard
`Project` work-item type, not a Product Line or Portfolio item, and not a Topic.
Current storage represents the layer through backlog product projection,
Product Map vision nodes, roadmap ledger goals, and bounded refs until a later
ticket accepts durable vision-record storage.

Rejected or deferred names remain explicit:

| Term | Decision | Reason |
| --- | --- | --- |
| `Project` hard type | Rejected for this run. | It duplicates product scope and would require enum, prefix, folder, KOA, view, and migration work. |
| `Theme` | Deferred alias only. | Useful for planning language, but too vague as canonical storage vocabulary. |
| `Objective` | Deferred alias only. | Better represented as roadmap ledger goals until objective storage is accepted. |
| Product Line / Portfolio | Catalog membership, not hierarchy. | Groups Initiatives for packaging and reporting without changing item parentage. |

## Semantics

| Concept | Canonical role | Not this |
| --- | --- | --- |
| Vision-layer record | Long-lived strategic intent, roadmap direction, and cross-product coordination scope. | A hard Markdown item type or replacement backlog hierarchy. |
| Initiative | Independently releasable component, module, or product narrative line under product or vision scope. | Product Line, Portfolio, release, or Topic. |
| Epic | Durable release or campaign story under an Initiative or legacy product scope. | Strategic vision, execution dependency, or release membership. |
| Feature | Reviewable capability or release-facing highlight. | Mandatory child layer under every Epic. |
| UserStory | Stakeholder value slice. | Structural requirement for all execution work. |
| Task / SubTask | Focused executable work; SubTask is the first-class child type when independently delegable under a Task. | Vision-layer storage or checklist fragments. |
| Work Intent | Metadata describing why an item exists and what result contract closes it. | Taxonomy tier or parent relationship. |
| Topic | Horizontal execution context and material bundle. | Vertical product hierarchy or parent edge. |
| Work Order | Agent execution or handoff context. | Durable structural parent. |

Vision-layer records may group Initiatives, Epics, Features, Topics, roadmap
goals, release records, ADRs, and evidence through typed projection edges. They
must not rewrite item `parent` fields, duplicate existing Epics, or turn Topic
membership into ownership.

## Bounded Reference Model

Cross-product and cross-backlog identity must use canonical refs rather than raw
paths. A ref may include these fields when the target kind needs them:

| Field | Purpose |
| --- | --- |
| `backlog_id` | Stable logical backlog root or configured backlog alias. |
| `product` | Backlog product slug. |
| `vision_id` | Vision-layer record id when durable storage or projection id exists. |
| `item_id` / `uid` | Work item id or immutable item uid. |
| `topic_id` | Topic association id; never a parent. |
| `adr_id` | ADR or decision record id. |
| `goal_id` | Version Goal Ledger goal id. |
| `release_id` | Release Record id. |
| `evidence_id` | Validation, artifact, report, or review evidence id. |

Diagnostics may include display labels and bounded ids, but they must not expose
raw local paths, arbitrary filesystem data, or unbounded scans outside configured
backlog roots. Invalid refs fail closed as diagnostics such as `missing_ref`,
`ambiguous_ref`, `stale_ref`, `orphan_epic`, `blocked_area`, or
`vision_scope_gap`.

## Roadmap And Status Projection

Backboard should project vision-layer status through Product Map and Version
Goal Ledger data:

1. Start from a Product Map `vision` node.
2. Traverse typed edges to Initiatives, Epics, Features, Topics, ADRs, roadmap
   goals, release records, and evidence refs.
3. Show gaps, stale refs, blocked areas, orphan legacy Epics, Review/Done
   evidence, and dogfood coverage as explicit diagnostics or evidence-quality
   fields.
4. Keep Review Inbox as the default screen; vision views are navigation and
   review context, not an execution scheduler.

KOBQL and saved-view implementation remain follow-up work. Existing focused
children cover that scope: `KOB-TSK-0053` for Product Map navigation,
`KOB-TSK-0054` and `KOB-TSK-0065` for roadmap ledger/status, `KOB-TSK-0055` for
decision debt, and `KOB-TSK-0084` for canvas rendering. Until those views are
implemented, Product Map schema fields and fixtures define the review contract.

## Agent Workflow Rules

Agents should attach substantial roadmap or command-center planning to a
vision-layer record when the work spans multiple Initiatives, Epics, products,
backlog roots, release lines, or dogfood programs. Normal item creation should
stay under Initiative, Epic, Feature, UserStory, or Task when the work has a
single structural owner.

Use a Topic when the agent needs a horizontal bundle of context, files, snippets,
or work-order claims across items. Relate that Topic to the vision-layer record
through a typed ref or Product Map edge; do not set `parent` to the Topic.

Use a Work Order only for execution or handoff evidence. A Work Order may point
back to the vision-layer record, Topic, and item it serves, but it does not own
the backlog item.

## Migration Guidance

Existing KOA, KOB, KFG, HUP, KOG, KTO, and Work Report planning work should be
linked to vision-layer projections before any structural migration. Existing
root Epics remain valid until evidence shows that Initiative or vision refs are
needed. Do not duplicate or silently reparent Epics to simulate a Project,
Theme, Product Line, Portfolio, release, Topic, or Work Order. When migration is
needed, record the source item ids, accepted semantics, rejected semantics,
rationale, and follow-up items using the semantic absorption protocol from
[Canonical Backlog Taxonomy](canonical-backlog-taxonomy.md).

## Non-Goals

- No hard `Project`, Product Line, or Portfolio item type.
- No new vision-layer item folder, id prefix, KOA connector schema, or Backboard
  mutation path.
- No automatic cross-backlog scan outside configured backlog roots.
- No agent dispatch, release publication, item remap, or topic cleanup.
