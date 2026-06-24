# Canonical Backlog Taxonomy

Status: accepted design contract for `KOB-FTR-0030`, `KOB-TSK-0086`, and the
semantic-absorption amendment from `KOB-TSK-0096`.

## Scope

This contract defines KOB taxonomy semantics and the intended structural parent
model. It does not implement new hard item types, release records, KOA contract
changes, HorizonPlugin migration, or Backboard mutation behavior.

Current hard formal item types remain `Epic`, `Feature`, `UserStory`, `Task`,
`Bug`, and `Issue`. `Project` is represented by the selected Project-equivalent
model from [Project Model Decision](project-model-decision.md). `SubTask` is a
role for independently delegable child work under a Task, represented with the
current Task storage until a future hard type is explicitly accepted.

## Relationship Axes

| Axis | Meaning | Not this |
| --- | --- | --- |
| `parent` / child | Structural ownership and intent inheritance. | Execution order or release scope. |
| `blocks` / `blocked_by` | Execution dependency. `A blocks B` means `A -> B`. | Product hierarchy. |
| `relates` | Non-blocking reference or context link. | Dependency or ownership. |
| Release membership | Version or publication scope. | Parent hierarchy. |
| Topic | Horizontal execution context and material bundle. | Vertical product hierarchy. |
| Work order | Execution or handoff context. | Durable structural parent. |

## Type Semantics

| Type or role | Canonical meaning | Admission rule |
| --- | --- | --- |
| Project-equivalent | Product-scale or vision-layer planning scope above Epic. | Use backlog product projection now; use vision-layer records when implemented. Do not create hard `Project` items in this run. |
| Epic | Durable deliverable outcome group under Project-equivalent scope. | Use when multiple capabilities, stories, tasks, or fixes share an outcome boundary. |
| Feature | Capability or release-facing highlight unit. | Use when the result is a coherent capability or likely release-note highlight. It is not mandatory under every Epic. |
| UserStory | Stakeholder or user value slice. | Use when value framing and outcome acceptance matter. It may sit under Feature or directly under Epic. |
| Task | Focused executable work. | Use for one bounded implementation, docs, validation, analysis, or migration unit. A Task may support UserStory, Feature, or Epic directly. |
| SubTask role | Independently delegable child work under a Task. | Use only when a distinct agent or thread can own it with its own output and validation. Otherwise keep the steps as a checklist or Worklog note. |
| Bug | Known incorrect behavior or regression. | Attach to the smallest useful ownership boundary: Task, Feature, Epic, or Project-equivalent. |
| Issue | Pre-triage unclear problem, risk, blocker, or runtime gap. | Attach to the smallest useful boundary when known; split into Task or Bug once actionable. |

## Flexible Parent Model

| Child | Model parent | Current storage rule |
| --- | --- | --- |
| Project-equivalent | none | Product projection or future vision-layer record. No `type: Project` item. |
| Epic | Project-equivalent | Root Epic under product scope until vision refs exist. |
| Feature | Epic | `parent` points to an Epic. |
| UserStory | Feature or Epic | `parent` points to a Feature or Epic. |
| Task | UserStory, Feature, or Epic | `parent` points to the closest useful structural owner. |
| SubTask role | Task | Store as a child `Task` under a Task only when independently delegable. |
| Bug | Task, Feature, Epic, or Project-equivalent | Use structural parent when local; use root/product scope or links when cross-epic. |
| Issue | Task, Feature, Epic, or Project-equivalent | Use structural parent when local; use root/product scope or links when not yet localized. |

Parent links must remain intra-product structural refs. Do not use release ids,
topic ids, work-order ids, raw paths, or product names as parent refs.

## Semantic Absorption

`KOB-TSK-0096` adds an explicit rule: duplicate or superseded tickets must not be
closed in a way that loses unique design intent. This taxonomy absorbs the useful
semantics from KOA-TSK-0211 and KOA-TSK-0212 as follows:

- Feature may be release-facing highlight material, not a mandatory layer under
  every Epic.
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

`KOB-TSK-0094`, `KOB-TSK-0095`, and `KOB-TSK-0096` are follow-up constraints under
this feature. This run references their design requirements but does not close
them as Done.

`Duplicate` must become semantically distinct from `Dropped`. A duplicate item
must preserve a canonical target link such as `duplicate_of`, surface that link in
views/search, and keep duplicate/canonical reporting available for reviewers.

The pre-create duplicate-search admission gate must record:

- search query;
- searched product or scope;
- candidate matches;
- candidate IDs that were actually read;
- create, update, or continue decision;
- override rationale when creating despite similar candidates.

Semantic absorption must record absorbed source item IDs, accepted semantics,
rejected semantics with rationale, and any downstream items left blocked rather
than duplicated.

## Non-goals

- No hard `Project` item type.
- No hard `SubTask` item type.
- No `items/project/` or `items/subtask/` directories.
- No Release Record, `releases/` directory, or KCC publication gate work.
- No KOA contract/view update or HorizonPlugin migration.
- No Backboard mutation behavior.
