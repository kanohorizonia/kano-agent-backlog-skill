# Hierarchy Validation Matrix

Status: accepted design contract for `KOB-TSK-0087` under `KOB-FTR-0030`, with
top-layer amendments from `KOB-TSK-0097` and `KOB-TSK-0098`.

This matrix defines how reparenting should be validated once hierarchy checks are
implemented or tightened. `KOB-TSK-0097` adds the hard Initiative item type.
`KOB-TSK-0098` defines Product Line / Portfolio membership as catalog metadata,
not a parent edge. This matrix remains a design contract for future parent
validation and does not add KOA contracts, metadata storage, or runtime
membership implementation.

## Edge Semantics

- `parent` / child is structural only. It expresses ownership, intent
  inheritance, and review grouping.
- `blocks` / `blocked_by` remains execution dependency. `A blocks B` means
  `A -> B`; `B blocked_by A` is the inverse.
- `relates` remains a non-blocking reference.
- Product Line / Portfolio membership is catalog grouping over Initiatives and
  must be represented by future membership metadata, not `parent`.
- Release membership, topics, and work orders are separate axes and must never be
  accepted as structural parents.
  Release scope is defined by [Release Record Schema](release-record-schema.md)
  and [Releases Directory Contract](releases-directory-contract.md) records, not
  by `parent` edits.

## Severity

| Severity | Meaning |
| --- | --- |
| Valid | The parent is canonical for the child. |
| Warning | The relationship can exist during migration or product-scope projection but should be reviewed. |
| Invalid | The reparent operation should fail before mutation. |
| Unsupported | The concept is valid in taxonomy but has no hard storage/schema support yet. |

## Parent Matrix

| Child | Valid parent | Warning | Invalid or unsupported |
| --- | --- | --- | --- |
| Product Line / Portfolio | none | Future catalog record outside item hierarchy may group Initiatives through membership metadata. | Hard `type: ProductLine`, hard `type: Portfolio`, `items/product-line/`, `items/portfolio/`, or parent edges are unsupported. |
| Project-equivalent | none | Product projection, future vision-layer ref, or Product Line / Portfolio catalog should be explicit outside item parentage. | Hard `type: Project` item is unsupported. |
| Initiative | Project-equivalent | Current storage keeps Initiative root-scoped under product projection. | Project hard type, UserStory, Task, Topic, Work Order, Release. |
| Epic | Initiative | Existing root Epics may remain while migrating to Initiative roots. | Feature, UserStory, Task, Bug, Issue, Topic, Work Order, Release. |
| Feature | Initiative or Epic | Direct Initiative parent is valid when no release/campaign Epic boundary is useful. | Project-equivalent as direct parent, UserStory, Task, Bug, Issue, Topic, Work Order, Release. |
| UserStory | Feature or Epic | Direct Epic parent should be used only when no release-facing Feature boundary is useful. | Task, Bug, Issue, Topic, Work Order, Release. |
| Task | UserStory, Feature, or Epic | Direct Epic or Feature parent is valid for foundation work without story boundaries. | Bug, Issue, Topic, Work Order, Release. |
| SubTask role | Task | Store as child `Task` under Task until hard SubTask support exists. | Non-Task parent; hard `type: SubTask` item is unsupported in this run. |
| Bug | Task, Feature, Epic, Initiative, or Project-equivalent | Project-equivalent attachment may be root/product scoped until vision refs exist. | UserStory unless the process explicitly chooses story-level bug ownership; Topic, Work Order, Release. |
| Issue | Task, Feature, Epic, Initiative, or Project-equivalent | Project-equivalent attachment may be root/product scoped until vision refs exist. | UserStory unless the scope is already localized by process policy; Topic, Work Order, Release. |

## Required Diagnostics

| Code | Severity | Message requirement |
| --- | --- | --- |
| `hierarchy.parent_unresolved` | Invalid | Parent ref must resolve uniquely in the active product. |
| `hierarchy.parent_cycle` | Invalid | Parent change would create a cycle. |
| `hierarchy.project_hard_type_unsupported` | Unsupported | `Project` is Project-equivalent only; do not create or parent to a hard Project item until a later implementation ticket accepts schema scope. |
| `hierarchy.product_line_as_parent_invalid` | Invalid | Product Line / Portfolio membership is catalog grouping, not structural hierarchy. Use future membership metadata, docs, or report grouping instead of `parent`. |
| `hierarchy.product_line_hard_type_unsupported` | Unsupported | Hard Product Line / Portfolio item storage is not implemented in this run. Do not create ProductLine or Portfolio enums, prefixes, or item folders. |
| `hierarchy.subtask_parent_invalid` | Invalid | SubTask-role work must be under a Task and independently delegable; checklist fragments stay in the parent Task body or Worklog. |
| `hierarchy.subtask_hard_type_unsupported` | Unsupported | Hard `SubTask` item storage is not implemented in this run. Use a child Task or checklist. |
| `hierarchy.release_as_parent_invalid` | Invalid | Release membership is release scope, not structural hierarchy. Use release inclusion metadata or links. |
| `hierarchy.topic_as_parent_invalid` | Invalid | Topic is horizontal execution context, not a parent. Use topic membership or relates links. |
| `hierarchy.work_order_as_parent_invalid` | Invalid | Work order is execution and handoff context, not durable item hierarchy. |
| `hierarchy.dependency_as_parent_invalid` | Invalid | Execution dependency belongs in `blocks` or `blocked_by`, not `parent`. |
| `hierarchy.relates_as_parent_invalid` | Invalid | Non-blocking reference belongs in `relates`, not `parent`. |

Diagnostics must name the child id, proposed parent ref, resolved parent type or
axis when available, and the suggested replacement edge. They must not silently
rewrite parent links.

## Reparent Preflight

Before mutating an item parent, future tooling should check:

1. Child and parent refs resolve uniquely in the active product.
2. Parent is not the child and does not create a cycle.
3. Parent belongs to a structural item type or the selected Project-equivalent
   model; Product Line / Portfolio membership is not a parent edge.
4. Parent is not a release, topic, work order, raw path, or external product name.
5. Child/parent pair is valid or intentionally warning-level according to the
   matrix above.
6. Any warning-level operation records rationale in Worklog or command evidence.

## Migration Notes

Existing root Epics remain acceptable while Initiative migration is incremental.
New durable component roots should use Initiative when they need independent
release or narrative ownership. Existing Task-to-Task relationships should be
reviewed as SubTask-role work: keep them only when they are independently
delegable and validated; move ordinary step lists back into the parent Task body
or Worklog.

Product Line / Portfolio changes are catalog migration, not hierarchy migration.
Future tools should update or report membership metadata when a Product Line
splits, merges, renames, or regroups Initiatives. They should not mutate
Initiative parent refs or move Initiative files to reflect market packaging.
