# Project Model Decision

Status: accepted design decision for `KOB-TSK-0088` under `KOB-FTR-0030`.

## Decision

KOB does not add `Project` as a hard formal work item type in this run. The
selected model is a vision-layer record subtype with a current alias/projection
over the backlog product. In taxonomy and validation docs, `Project-equivalent`
means that selected model, not a literal `type: Project` Markdown item.

Until the vision-layer model is implemented, project-scale scope is represented
by the backlog product plus root Epics, product map projections, and explicit
links or worklogs. Agents must not create `items/project/`, `PRJ` IDs, or
`type: Project` frontmatter from this decision.

## Options Evaluated

| Option | Decision | Rationale | Schema impact | Migration impact | Follow-up tickets |
| --- | --- | --- | --- | --- | --- |
| Hard formal `Project` item type | Rejected for now; revisit only with explicit implementation scope. | It duplicates backlog product semantics and would touch enums, ID prefixes, folders, KOA schemas, views, state sync, and Backboard before the vision layer is settled. | Would require `ItemType::Project`, a prefix, `items/project/`, parser/storage/view/schema updates, KOA contract updates, and tests. None are implemented in this run. | Existing root Epics and product-scoped planning would need migration or aliasing. Premature migration could create conflicting roots. | Create a dedicated implementation ticket only if a later decision chooses the hard type. |
| Vision-layer record subtype | Selected design direction. | KOB already has a need for strategic intent above Epic (`KOB-FTR-0023`). A vision-layer record can carry long-lived scope without expanding every work-item enum immediately. | No hard item-type change in this run. Future schema belongs to the vision-layer work, not this taxonomy doc update. | Existing product and roadmap intent can be linked to vision records without moving item files. | Continue through `KOB-FTR-0023`; create implementation tickets there if needed. |
| Alias/projection over backlog product | Selected current representation. | The backlog product is already the operational scope boundary. Backboard/Product Map can project a product as project-scale context while the durable hierarchy still starts at Epic. | No new storage. Product Map may show a project-scale node derived from product/vision data. | Existing items stay in place. Root Epics remain valid and can be grouped by the product projection. | Covered by Product Map and vision-layer follow-ups. |
| Deferred | Partially selected for hard-type implementation only. | The taxonomy needs a decision now so downstream KOA work does not keep treating `Project` as already implemented. | Hard-type schema work is deferred. Documentation and validation contracts proceed now. | No bulk migration. Record source semantics and revisit when implementation scope is explicit. | No hard-type ticket is required from this decision. |

## Rationale

- KOB currently has a backlog product boundary, Product Map projections, and an
  active vision-layer feature. Adding another top-level hard item before those
  settle would create two competing concepts for product-scale planning.
- `KOB-TSK-0031` intentionally added only `Issue` as a new hard item type and
  kept other planning/work-intent concepts metadata-first. This decision follows
  that restraint.
- KOA-side `Project` semantics remain valuable input, but they are not canonical
  authority for KOB schema changes.
- Backboard can render project-scale context from product/vision projections
  without requiring a new item folder or enum.

## Parent Semantics

The model-level hierarchy is:

| Child | Model parent |
| --- | --- |
| Project-equivalent | none |
| Epic | Project-equivalent |
| Feature | Epic |
| UserStory | Feature or Epic |
| Task | UserStory, Feature, or Epic |
| SubTask role | Task |
| Bug | Task, Feature, Epic, or Project-equivalent |
| Issue | Task, Feature, Epic, or Project-equivalent |

Because `Project-equivalent` is not a hard item type, current storage represents
that parent through product scope and future vision-layer refs. Do not put a
product name, topic id, release id, or work-order id into `parent` to simulate a
Project.

## Schema Impact

This decision changes documentation and validation policy only.

- No `Project` enum value is added.
- No `items/project/` directory is added.
- No `PRJ` prefix is allocated.
- No KOA schema or ChatGPT-facing connector contract is changed.
- No Backboard mutation path is added.
- Parent refs remain structural item refs only; product, release, topic, and work
  order associations stay separate axes.

## Migration Impact

- Existing root Epics remain valid while Project-equivalent storage is product or
  vision projected.
- Existing Features are not forced under every Epic when they only serve as
  release-facing highlight material.
- Existing Task, Bug, and Issue items do not need retyping to conform to this
  decision.
- KOA-TSK-0211 and KOA-TSK-0212 are treated as duplicate or superseded input
  because KOB lacks a real `Duplicate` state today. Their semantics are absorbed
  here and in the canonical taxonomy contract instead of abandoned.
- KOA-TSK-0213 and KOA-TSK-0214 remain downstream KOA/HorizonPlugin work blocked
  on this KOB taxonomy decision and future schema exposure.
- KOA-TSK-0215 and KOA-TSK-0216 are release/KCC convention inputs and are not
  implemented by this taxonomy run.

## Semantic Absorption

This decision intentionally absorbs the following prior KOA-side semantics into
canonical KOB taxonomy work:

- Feature may be release-facing highlight material, not a mandatory layer under
  every Epic.
- Task may directly support Epic, Feature, or UserStory.
- SubTask is independently delegable coding-agent or thread work under Task, not
  a checklist primitive.
- Release membership is release scope, not hierarchy.
- Topic is horizontal execution context.
- Work order is execution and handoff context.
- `Project` must not be added as a hard type until a decision records
  alternatives, schema impact, migration impact, and accepted implementation
  scope.

## Revisit Conditions

Reopen this decision only when at least one of these is true:

- the vision-layer implementation proves product projection is insufficient;
- Backboard or KOA needs stable project refs that cannot be represented by
  product or vision records;
- migration evidence shows root Epics cannot safely carry project-scale planning;
- a dedicated implementation ticket accepts enum, storage, KOA, view, and test
  scope for a hard `Project` type.
