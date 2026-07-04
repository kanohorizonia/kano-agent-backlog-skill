# Project Model Decision

Status: accepted design decision for `KOB-TSK-0088` under `KOB-FTR-0030`, with
top-layer amendments from `KOB-TSK-0097` and `KOB-TSK-0098`.

## Decision

KOB does not add `Project` as a hard formal work item type in this run. The
selected model is a vision-layer record subtype with a current alias/projection
over the backlog product. In taxonomy and validation docs, `Project-equivalent`
means that selected model, not a literal `type: Project` Markdown item.

Until the vision-layer model is implemented, project-scale scope is represented
by the backlog product plus root Initiatives or legacy root Epics, product map
projections, and explicit links or worklogs. Agents must not create
`items/project/`, `PRJ` IDs, or `type: Project` frontmatter from this decision.

## KOB-TSK-0097 Amendment

`KOB-TSK-0097` does not reverse the hard `Project` rejection above. It accepts a
different hard item type: `Initiative`, stored under `items/initiative` with
`INIT` display IDs. Initiative avoids the overloaded Project term and represents
an independently releasable component, module, or product narrative line above
Epic and Feature.

The amended top hierarchy is Project-equivalent product/vision scope ->
Initiative -> Epic or Feature. Epic remains optional and useful for release or
campaign story grouping. Product Line / Portfolio grouping above Initiative is a
separate catalog membership model tracked by `KOB-TSK-0098`, not a parent item in
this decision.

## KOB-TSK-0098 Amendment

`KOB-TSK-0098` defines Product Line / Portfolio as a catalog-membership layer
above Initiative, not a new structural parent or hard work-item type. Product
Line / Portfolio membership expresses market packaging, bundle families, roadmap
rollups, or portfolio navigation across Initiatives. It must not rewrite
Initiative parentage, ids, folders, worklogs, child items, or validation history.

The Product Line / Portfolio model is intentionally edge-based. Future schema may
store stable catalog records and explicit membership edges from a Product Line or
Portfolio to Initiative ids, including role, status, effective dates, rationale,
and optional release or bundle references. This decision does not create
`ProductLine` or `Portfolio` enum values, `items/product-line/`,
`items/portfolio/`, parent-link validation support, product-map schema changes,
CLI commands, KOA contracts, or Backboard mutation behavior.

Because Product Line membership is catalog metadata, packaging changes should be
auditable membership changes rather than backlog reparenting. A Product Line can
split, merge, rename, or regroup Initiatives while each Initiative remains the
stable independently releasable narrative root for its component/module/product
line.

## Options Evaluated

| Option | Decision | Rationale | Schema impact | Migration impact | Follow-up tickets |
| --- | --- | --- | --- | --- | --- |
| Hard formal `Project` item type | Rejected for now; revisit only with explicit implementation scope. | It duplicates backlog product semantics and would touch enums, ID prefixes, folders, KOA schemas, views, state sync, and Backboard before the vision layer is settled. | Would require `ItemType::Project`, a prefix, `items/project/`, parser/storage/view/schema updates, KOA contract updates, and tests. None are implemented in this run. | Existing root Epics and product-scoped planning would need migration or aliasing. Premature migration could create conflicting roots. | Create a dedicated implementation ticket only if a later decision chooses the hard type. |
| Vision-layer record subtype | Selected design direction. | KOB already has a need for strategic intent above Epic (`KOB-FTR-0023`). A vision-layer record can carry long-lived scope without expanding every work-item enum immediately. | No hard item-type change in this run. Future schema belongs to the vision-layer work, not this taxonomy doc update. | Existing product and roadmap intent can be linked to vision records without moving item files. | Continue through `KOB-FTR-0023`; create implementation tickets there if needed. |
| Alias/projection over backlog product | Selected current representation. | The backlog product is already the operational scope boundary. Backboard/Product Map can project a product as project-scale context while the durable hierarchy still starts at Epic. | No new storage. Product Map may show a project-scale node derived from product/vision data. | Existing items stay in place. Root Epics remain valid and can be grouped by the product projection. | Covered by Product Map and vision-layer follow-ups. |
| Product Line / Portfolio membership over Initiatives | Selected as catalog model only. | Market packaging and portfolio grouping change more often than Initiative ownership. Membership edges preserve stable Initiative history while allowing Product Line views, bundles, and reports. | No hard item type or runtime schema in this ticket. Future schema may add catalog records or membership edges outside item hierarchy. | Initiative item files, parent refs, children, and worklogs stay stable during grouping, split, merge, and rename events. | Create a dedicated implementation ticket only when metadata storage, Product Map projection, CLI/API, or reports are accepted. |
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

The model-level hierarchy, as amended by `KOB-TSK-0097` and `KOB-TSK-0098`, is:

| Child | Model parent |
| --- | --- |
| Product Line / Portfolio | none |
| Project-equivalent | none |
| Initiative | Project-equivalent |
| Epic | Initiative |
| Feature | Initiative or Epic |
| UserStory | Feature or Epic |
| Task | UserStory, Feature, or Epic |
| SubTask | Task |
| Bug | Task, Feature, Epic, Initiative, or Project-equivalent |
| Issue | Task, Feature, Epic, Initiative, or Project-equivalent |

Because `Project-equivalent` is not a hard item type, current storage represents
that parent through product scope and future vision-layer refs. `Initiative` is
the hard root item beneath that scope when a durable component narrative is
needed. Do not put a product name, topic id, release id, or work-order id into
`parent` to simulate a Project.

Product Line / Portfolio membership also stays outside `parent`. Do not put a
Product Line name, Portfolio name, catalog id, bundle id, or market package into
`parent` to group Initiatives. Use future membership metadata or current
documentation/reporting notes until that metadata exists.

## Schema Impact

This `KOB-TSK-0088` decision changed documentation and validation policy only.
`KOB-TSK-0097` separately adds hard Initiative schema support. `KOB-TSK-0098`
defines Product Line / Portfolio membership semantics only.

- No `Project` enum value is added.
- No `ProductLine` or `Portfolio` enum value is added.
- No `items/project/` directory is added.
- No `items/product-line/` or `items/portfolio/` directory is added.
- No `PRJ` prefix is allocated.
- No Product Line / Portfolio prefix is allocated.
- No KOA schema or ChatGPT-facing connector contract is changed.
- No Backboard mutation path is added.
- No Product Map projection schema change is made by `KOB-TSK-0098`.
- Parent refs remain structural item refs only; product, release, topic, and work
  order associations stay separate axes.

## Migration Impact

- Existing root Epics remain valid during migration, but new durable component
  roots should use Initiative when an independently releasable narrative line is
  needed.
- Existing Features are not forced under every Epic when they only serve as
  release-facing highlight material.
- Existing Initiatives are not reparented when Product Line / Portfolio grouping
  changes. Current grouping can be recorded in docs or reports until explicit
  membership metadata exists.
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
- Initiative now carries independently releasable component/module/product
  narrative ownership without adding hard `Project` semantics.
- Task may directly support Epic, Feature, or UserStory.
- SubTask is independently delegable coding-agent or thread work under Task, not
  a checklist primitive.
- Release membership is release scope, not hierarchy.
- Product Line / Portfolio membership is catalog grouping over Initiatives, not
  hierarchy.
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
