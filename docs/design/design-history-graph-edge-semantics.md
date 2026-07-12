# Design-History Graph Edge Semantics

Status: draft schema contract for Product Memory design-history edges.

## Goal

Design-history graph edges explain how ideas, ADRs, feature events, evidence, and
implementation records influenced each other. This graph is not the execution
dependency graph and must not be used for `blocks` cycle detection.

## Edge Types

| Edge type | Direction | Meaning |
| --- | --- | --- |
| `led_to` | source -> outcome | An idea or event contributed to a later decision or state. |
| `rejected_by` | option -> decision | A decision rejected an option. |
| `superseded_by` | old -> new | A newer decision or event replaced an older one. |
| `validated_by` | claim -> evidence | Evidence supports the source claim. |
| `invalidated_by` | claim -> evidence | Evidence contradicts the source claim. |
| `implemented_by` | decision -> work_order | A work item implemented or attempted the decision. |
| `motivated_by` | decision/event -> evidence_or_incident | Source exists because of another recorded fact. |

Allowed node types are Product Map node ids, ADR ids, feature evolution event ids,
work item ids, and evidence ids. Rendering can group cycles as historical loops,
but dependency-cycle detection must ignore these edges because they are not
execution blockers.

## Boundary From Execution Edges

Do not map these design-history edges onto `parent`, `blocks`, `blocked_by`, or
work-order scheduling semantics. A design decision can be motivated by an
incident and implemented by a task without implying that the incident blocks the
task.

## Cycle Audit Boundary

The Backboard cycle audit uses only normalized `blocks` and `blocked_by`
execution edges, with direction normalized from blocker to blocked. `parent`,
`relates`, topic-membership, and every design-history edge in this document never
participate in cycle detection. Its count is the number of cyclic strongly
connected components in the visible bounded dependency graph, not a count of
simple loops and not a backlog-global count.

## Backboard Rules

- Show edge labels and source refs so reviewers can challenge the explanation.
- Display missing nodes as diagnostics, not hidden omissions.
- Never start agents or change backlog state from this graph.
- Keep Ark Console execution admission separate from Backboard design history.

## Fixture

The schema and example fixture live in:

- [design-history-graph.schema.json](../../references/design-history-graph.schema.json)
- [design-history-graph.fixture.json](../../references/design-history-graph.fixture.json)
