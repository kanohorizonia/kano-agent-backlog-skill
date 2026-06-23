# ADR Lifecycle Metadata

Status: draft schema contract for Product Memory ADR lifecycle records.

## Goal

ADR lifecycle metadata lets Backboard show whether a decision is accepted,
rejected, superseded, stale, or waiting for revisit. It is design memory, not an
execution queue and not a replacement for ADR prose.

## Metadata Fields

| Field | Meaning |
| --- | --- |
| `decision_status` | Current lifecycle state of the decision. |
| `feature_refs` | Product features or work items affected by the decision. |
| `accepted_option` | The selected option when the decision is accepted. |
| `rejected_options` | Alternatives explicitly rejected and why. |
| `evidence_refs` | Validation, review, or artifact refs that support the decision. |
| `superseded_by` | ADR refs that replace this decision. |
| `revisit_condition` | Condition that should trigger renewed review. |

Allowed `decision_status` values are `accepted`, `rejected`, `superseded`,
`revisit_needed`, and `stale`. Draft or legacy ADRs without metadata should be
reported as metadata gaps, not silently promoted to accepted decisions.

## Backboard Rules

- Backboard may display ADR lifecycle badges and evidence gaps.
- Backboard must not edit ADR status or auto-supersede decisions.
- Missing metadata on old ADRs is a diagnostic, not a parse failure.
- Ark Console execution admission is separate from ADR lifecycle state.

## Fixture

The schema and example fixture live in:

- [adr-lifecycle-metadata.schema.json](../../references/adr-lifecycle-metadata.schema.json)
- [adr-lifecycle-metadata.fixture.json](../../references/adr-lifecycle-metadata.fixture.json)
