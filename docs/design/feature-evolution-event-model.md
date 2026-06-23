# Feature Evolution Event Model

Status: draft schema contract for Product Memory feature history.

## Goal

Feature Evolution records how a feature changed over time without pretending
that missing history is known. Backboard can use the event stream to explain why
the current state exists and where evidence is weak.

## Event Types

Allowed event types are:

- `idea`
- `decision`
- `rejected_option`
- `spike`
- `implementation_version`
- `validation`
- `incident`
- `migration`
- `current_state`
- `debt`
- `revisit_condition`

Every event has `event_id`, `event_type`, `feature_ref`, `summary`, `occurred_at`,
and `source_refs`. When the date is unknown, use `occurred_at: "unknown"` and
include a diagnostic instead of inventing history.

## Evidence Rules

- ADRs can be source refs for decision events, but an ADR is not the entire
  feature history.
- Validation events should cite reports, artifacts, or worklog evidence where
  available.
- Incidents and debt events should remain visible even when they do not imply a
  current defect.
- Backboard must show missing or stale evidence as a gap, not as a failure hidden
  inside the projection.

## Non-Goals

- No automatic reconstruction from git history.
- No unsupported claims about why an old design changed.
- No execution dependency graph semantics.
- No Ark Console mutation or agent dispatch.

## Fixture

The schema and example fixture live in:

- [feature-evolution-event.schema.json](../../references/feature-evolution-event.schema.json)
- [feature-evolution-event.fixture.json](../../references/feature-evolution-event.fixture.json)
