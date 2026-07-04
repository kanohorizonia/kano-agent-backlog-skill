# Canonical Backlog Taxonomy

Status: accepted design contract for `KOB-FTR-0030`, `KOB-FTR-0021`, `KOB-TSK-0086`, the
semantic-absorption amendment from `KOB-TSK-0096`, the hard Initiative amendment
from `KOB-TSK-0097`, the Product Line / Portfolio membership definition from
`KOB-TSK-0098`, and the hard SubTask amendment from `KOB-TSK-0111`.

## Scope

This contract defines KOB taxonomy semantics and the intended structural parent
model. `KOB-TSK-0097` adds the hard `Initiative` item type.
`KOB-TSK-0098` defines Product Line / Portfolio as catalog membership above
Initiative. `KOB-TSK-0111` adds the hard `SubTask` item type for independently
delegable executable child work under a Task. These decisions do not implement
hard `Project`, Product Line, or Portfolio item types; Product Line storage/schema/CLI/reporting;
release records; KOA contract changes; HorizonPlugin migration; or Backboard
mutation behavior.

Current hard formal item types are `Initiative`, `Epic`, `Feature`, `UserStory`,
`Task`, `SubTask`, `Bug`, and `Issue`. `Project` remains rejected as a hard item type by
[Project Model Decision](project-model-decision.md); `Initiative` is the
KOB-TSK-0097 amendment for an independently releasable component/module/product
narrative layer. `SubTask` is the KOB-TSK-0111 amendment for independently
delegable child work under a Task and uses `SUBTSK` display IDs under
`items/subtask/`.

Work Intent is metadata on those item types, not another taxonomy tier. The
accepted metadata fields are `work_intent`, `execution_mode`, `result_contract`,
`evidence_requirement`, `follow_up_policy`, and `no_go_or_defer_policy`, plus
passive provenance fields `intent.author`, `intent.source`, `intent.owner`,
`intent.rationale`, `intent.reviewers`, `intent.provenance_refs`,
`intent.conflicts_with`, and `intent.supersedes`.
Allowed `work_intent` values are `implementation`, `investigation`, `spike`,
`decision`, `experiment`, `validation`, `audit`, `migration`,
`policy_contract`, `decomposition`, `planning`, `docs_only`,
`review_admission`, `parent_reconciliation`, `runbook`, `incident`, and
`deprecation`.

## Relationship Axes

| Axis | Meaning | Not this |
| --- | --- | --- |
| `parent` / child | Structural ownership and intent inheritance. | Execution order or release scope. |
| `blocks` / `blocked_by` | Execution dependency. `A blocks B` means `A -> B`. | Product hierarchy. |
| `relates` | Non-blocking reference or context link. | Dependency or ownership. |
| Product Line / Portfolio membership | Catalog or market-packaging grouping over Initiatives. | Structural ownership or backlog history. |
| Release membership | Version or publication scope. | Parent hierarchy. |
| Topic | Horizontal execution context and material bundle. | Vertical product hierarchy. |
| Work order | Execution or handoff context. | Durable structural parent. |

## Type Semantics

| Type or role | Canonical meaning | Admission rule |
| --- | --- | --- |
| Product Line / Portfolio | Catalog, market-packaging, or bundle grouping above Initiative. | Use future membership metadata over Initiatives. Do not create hard Product Line or Portfolio items, folders, enums, or parent refs. |
| Project-equivalent | Product-scale or vision-layer planning scope above Initiative. | Use backlog product projection now; use vision-layer records when implemented. Do not create hard `Project` items. |
| Initiative | Independently releasable component, module, or product narrative layer. | Use when a durable line can be released, maintained, presented, and explained independently across multiple Epics or Features. |
| Epic | Durable release/campaign story under an Initiative. | Use when multiple capabilities, stories, tasks, or fixes share a coherent outcome boundary. Epic remains optional for early or MVP apps. |
| Feature | Capability or release-facing highlight unit under an Initiative or Epic. | Use when the result is a coherent capability or likely release-note highlight. It is not mandatory under every Epic. |
| UserStory | Stakeholder or user value slice. | Use when value framing and outcome acceptance matter. It may sit under Feature or directly under Epic. |
| Task | Focused executable work. | Use for one bounded implementation, docs, validation, analysis, or migration unit. A Task may support UserStory, Feature, or Epic directly. |
| SubTask | Independently delegable executable child work under a Task. | Use only when a distinct agent or thread can own it with its own output and validation. Otherwise keep the steps as a checklist or Worklog note. |
| Bug | Known incorrect behavior or regression. | Attach to the smallest useful ownership boundary: Task, Feature, Epic, Initiative, or Project-equivalent. |
| Issue | Pre-triage unclear problem, risk, blocker, or runtime gap. | Attach to the smallest useful boundary when known; split into Task or Bug once actionable. Initiative is valid for component-level unclear scope. |

## Work Intent Metadata Contract

`KOB-FTR-0021` keeps investigation, decision, spike, experiment, validation,
audit, migration, policy contract, runbook, incident, and deprecation work as
metadata-first intent on existing items. It does not add hard Research, Decision,
Spike, Experiment, Validation, Audit, Migration, Policy, Runbook, Incident, or
Deprecation item types, directories, ID prefixes, or parent rules.

For non-implementation intents, the Done gate is the result contract, not a code
change. A no-code investigation or decision can close when the result is recorded
in the item, ADR, artifact, or worklog; evidence is inspectable; the follow-up
policy says whether implementation tickets were created; and the no-go/defer
policy records why no change follows. If implementation is required, create or
link a follow-up Task/Bug/Feature and keep the original no-code item as the
decision or investigation record.

`KOB-TSK-0071` reserves the `intent.*` fields for future multi-human provenance
and conflict visibility. They are optional extension points only; this taxonomy
does not add governance, voting, authority rules, permission policy, or automated
intent conflict resolution.

## Flexible Parent Model

| Child | Model parent | Current storage rule |
| --- | --- | --- |
| Product Line / Portfolio | none | Future catalog outside item hierarchy. No `type: ProductLine`, `type: Portfolio`, `items/product-line/`, or `items/portfolio/`. |
| Project-equivalent | none | Product projection or future vision-layer record outside item hierarchy. No `type: Project` item. |
| Initiative | Project-equivalent | Root Initiative under product scope. Store as `items/initiative` with `INIT` display IDs. |
| Epic | Initiative | `parent` points to an Initiative; existing root Epics may remain during migration. |
| Feature | Initiative or Epic | `parent` points to the closest useful Initiative or Epic boundary. |
| UserStory | Feature or Epic | `parent` points to a Feature or Epic. |
| Task | UserStory, Feature, or Epic | `parent` points to the closest useful structural owner. |
| SubTask | Task | Store as `items/subtask/<bucket>/<PREFIX>-SUBTSK-<NNNN>_...md` with `parent` pointing to a Task. |
| Bug | Task, Feature, Epic, Initiative, or Project-equivalent | Use structural parent when local; use Initiative or root/product scope when cross-epic. |
| Issue | Task, Feature, Epic, Initiative, or Project-equivalent | Use structural parent when local; use Initiative or root/product scope when not yet localized. |

Parent links must remain intra-product structural refs. Do not use release ids,
topic ids, work-order ids, raw paths, or product names as parent refs.

## Product Line / Portfolio Membership

`KOB-TSK-0098` defines Product Line / Portfolio as catalog membership over
Initiatives, not another durable work-item parent. A Product Line may group one
or more Initiatives for market packaging, roadmap reporting, commercial bundles,
documentation navigation, or release-family communication. An Initiative keeps
its own identity, parentage, worklog, children, validation history, and release
narrative even when the Product Line grouping changes.

The selected model is membership metadata, not structural reparenting:

- The durable edge is `product_line_or_portfolio -> Initiative` membership, not
  `Initiative.parent = product_line`.
- Product Line changes may add, remove, rename, split, merge, or time-bound
  membership edges without moving Initiative item files or rewriting Initiative
  ancestry.
- A single Initiative may appear in more than one catalog view only when future
  metadata explicitly supports multi-membership roles, such as primary,
  secondary, bundle, or deprecated membership.
- Historical reports should evaluate membership as of the report date when
  time-bounded membership metadata exists; otherwise they should state that the
  current catalog view is being projected over stable Initiative history.

Future implementation may choose one of these schema surfaces, but this ticket
does not implement any of them:

- product-map projection nodes with stable Product Line / Portfolio ids;
- catalog records under a non-item metadata namespace, such as `_meta`;
- explicit membership edge records containing catalog id, Initiative id,
  membership role, status, effective dates, rationale, and optional release or
  bundle references.

View and report behavior should derive grouping from membership metadata when it
exists. Backlog item tree views should continue to render Initiative as the
structural root below product or vision scope. Product Line views may group
Initiatives, show unassigned Initiatives, and aggregate release/bundle summaries,
but must not imply that catalog movement changes backlog ownership.

Split and merge scenarios are catalog operations. Splitting a Product Line should
create new catalog groupings and move membership edges, not split Initiative
history. Merging Product Lines should consolidate membership edges and keep
Initiative ids stable. If a component itself needs independent evolution,
release identity, and backlog ownership, model it as an Initiative rather than
as a hidden child of a Product Line.

## Semantic Absorption

`KOB-TSK-0096` adds an explicit rule: duplicate or superseded tickets must not be
closed in a way that loses unique design intent. This taxonomy absorbs the useful
semantics from KOA-TSK-0211 and KOA-TSK-0212 as follows:

- Feature may be release-facing highlight material, not a mandatory layer under
  every Epic.
- Initiative is the independently releasable component/module/product narrative
  layer above Epic and Feature.
- Product Line / Portfolio grouping above Initiative is catalog membership, not a
  structural parent in this taxonomy run.
- Task may directly support Epic, Feature, or UserStory.
- SubTask is independently delegable coding-agent or thread work under Task, not
  a checklist primitive.
- Release membership is release scope, not hierarchy.
- Topic is horizontal execution context.
- Work order is execution and handoff context.
- `Project` must not be added as a hard type until `KOB-TSK-0088` records the
  decision with alternatives, schema impact, migration impact, and accepted
  implementation scope.

Rejected or deferred semantics must be recorded with rationale. KOA-TSK-0213 and
KOA-TSK-0214 are not duplicates; they remain blocked downstream work. KOA-TSK-0215
and KOA-TSK-0216 are release/KCC inputs and are out of scope for this taxonomy
implementation run.

## Semantic Absorption Protocol

Before changing a duplicate or superseded source item to `Duplicate`, `Dropped`,
or another terminal/non-active state, the agent must inspect the source item and
record one of these outcomes on the canonical target:

- absorbed semantics with source item IDs;
- no-new-semantics statement with evidence that the source was read;
- rejected semantics with explicit rationale;
- blocked downstream semantics that are not duplicates and must stay as separate
  follow-up work.

Absorption review must consider decisions, constraints, risks, examples,
acceptance criteria, artifact links, and implementation scope. It must happen
before state mutation, not as a cleanup note after the source item has already
been hidden from active views.

Suggested Worklog template for the canonical item:

```text
Semantic absorption: source=<ITEM_ID>[,<ITEM_ID>]; outcome=<absorbed|no-new-semantics|rejected|blocked-downstream>; absorbed_decisions=<summary>; absorbed_constraints=<summary>; absorbed_risks=<summary>; absorbed_examples=<summary>; absorbed_artifacts=<paths-or-none>; rejected_semantics=<summary-or-none>; rationale=<why>; follow_up=<items-or-none>
```

Suggested Worklog template for the source item:

```text
Duplicate reconciliation: canonical_target=<ITEM_ID>; absorption_recorded_on=<ITEM_ID>; outcome=<duplicate|superseded|blocked-downstream|not-duplicate>; summary=<what was absorbed or why none>; next=<state or follow-up>
```

## Duplicate and Admission Follow-ups

`KOB-TSK-0094` and `KOB-TSK-0095` implement the native Duplicate state and
pre-create duplicate-search admission gate. `KOB-TSK-0096` remains the semantic
absorption follow-up for richer reconciliation evidence.

`Duplicate` is semantically distinct from `Dropped`. A duplicate item preserves a
canonical target link in `duplicate_of`, surfaces that link in views/search, and
keeps duplicate/canonical reporting available for reviewers. The Duplicate
transition requires `--duplicate-of` and rejects self-references.

The pre-create duplicate-search admission gate records:

- search query;
- searched product or scope;
- candidate matches;
- candidate IDs that were actually read;
- create, update, or continue decision;
- override rationale when creating despite similar candidates.

Native create commands fail closed without search query, searched scope, and a
decision. Candidate matches must also appear in the read-candidate list, and
creating despite candidates requires explicit override plus rationale. Each
accepted create appends Worklog evidence and writes a receipt under
`_meta/duplicate-admission/<item-id>.json`.

Semantic absorption must record absorbed source item IDs, accepted semantics,
rejected semantics with rationale, and any downstream items left blocked rather
than duplicated.

## Non-goals

- No hard `Project` item type.
- No hard Research, Decision, Spike, Experiment, Validation, Audit, Migration,
  Policy, Runbook, Incident, or Deprecation item types; use Work Intent metadata.
- No `items/project/` directory.
- No Product Line / Portfolio hard item type, storage, CLI/API, or runtime
  membership implementation.
- No Release Record, `releases/` directory, or KCC publication gate work.
- No KOA contract/view update or HorizonPlugin migration.
- No Backboard mutation behavior.

## SubTask migration guidance

Existing Task-under-Task workaround items are historical evidence and must not be
auto-migrated. Review candidates explicitly, report which Task children are truly
independently delegable SubTask work, and migrate only with a deliberate item move
or rewrite that preserves IDs/history according to the chosen maintenance plan.
Ordinary checklist fragments should remain in the parent Task body or Worklog.
