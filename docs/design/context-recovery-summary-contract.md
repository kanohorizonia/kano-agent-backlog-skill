# Context Recovery Summary Contract

Status: draft schema contract for Backboard context recovery summaries.

## Goal

Context recovery summaries help humans and agents resume work without reading
every ticket. They are a recovery lens, not a source of truth. Every claim should
link to raw records or be labeled unsupported, stale, or unsafe to touch.

## Required Sections

| Section | Purpose |
| --- | --- |
| `area_summary` | Short description of the area being recovered. |
| `current_state` | What is known to be true now, with refs. |
| `key_decisions` | ADRs or decisions that shape the area. |
| `active_risks` | Current risks, blockers, or uncertainty. |
| `evidence` | Validation, artifacts, worklogs, and confidence notes. |
| `next_actions` | Recommended next human or agent-safe actions. |
| `do_not_touch` | Unsafe areas, boundaries, or explicit non-goals. |

## Safety Rules

- No private local paths or secrets are required.
- Unsupported claims stay labeled as unsupported.
- Stale evidence must remain visible.
- Backboard may display summaries and gaps, but must not start agents.
- Ark Console execution approval remains separate.

## Fixture

The schema and example fixture live in:

- [context-recovery-summary.schema.json](../../references/context-recovery-summary.schema.json)
- [context-recovery-summary.fixture.json](../../references/context-recovery-summary.fixture.json)
