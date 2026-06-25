# Release Readiness Review Gate

Status: draft gate contract for KOB release readiness review.

## Goal

The release readiness gate is a bounded review report that decides whether a
release record has enough evidence to advance release claims. It is not a
publishing action, does not create release content, and does not treat closed
items as sufficient proof of release readiness.

The gate integrates the Release Record, releases workspace manifest, release
notes evidence bundle, validation evidence, dogfood evidence, known limitations,
and human approval markers into one pass/fail/blocked/unknown report.

## Gate Categories

Every readiness report records one result for each category:

| Category | Required question |
| --- | --- |
| `included_item_state` | Are included items in a state compatible with the claimed release scope? |
| `validation_evidence` | Is validation evidence present for the release claims being made? |
| `dogfood_evidence` | Is dogfood evidence present, or is missing dogfood scope explicitly deferred? |
| `release_notes_evidence` | Do release notes trace to a release-notes evidence bundle and preserve missing evidence? |
| `public_docs_draft` | Are public-facing docs drafts staged as drafts without live publication claims? |
| `known_limitations` | Are limitations visible, sourced, and reflected in drafts? |
| `unresolved_blockers` | Are unresolved blockers listed with owners or follow-up items? |
| `human_approval` | Has the required human review passed for the release claim level? |

Each category status is one of `pass`, `fail`, `blocked`, or `unknown`. Missing
evidence must be recorded as an actionable entry with the claim, the required
source, the reason it is missing, and an owner or follow-up item.

## Early Dogfood Scope Policy

Early dogfood releases may defer or cut scope, but only with explicit rationale.
A readiness report can record a `scope_adjustment` for dogfood-only or
internally validated release lines. The adjustment must say which category it
affects, whether scope is deferred or cut, why the decision is acceptable, and
which refs support the decision.

Deferring scope is not the same as passing evidence. Public availability,
downloadability, package-manager availability, marketplace readiness, or official
release-page visibility still require publication evidence and human approval.

## Claim Guard

The gate prevents unverified release claims:

- A `released` state is not enough to claim public publication.
- Missing evidence must remain visible as `missing_evidence`, blockers, or
  scoped deferrals.
- Public availability is allowed only after the public docs publishing handoff
  records publication evidence and human approval.
- Human approval is required before a readiness report can support public-facing
  release claims.

## Integration Points

- `release-record.schema.json` may reference readiness reports through
  `readiness_gate_refs`.
- `releases-directory-contract.schema.json` records readiness reports as bounded
  evidence refs under the release workspace `evidence/` tree.
- `release-notes-evidence-bundle.schema.json` provides release-note source refs
  and missing-evidence entries consumed by the readiness report.
- `public-docs-publishing-handoff.schema.json` owns the later transition from
  approved drafts to publication refs.

## Fixture

The schema and example fixture live in:

- [release-readiness-gate.schema.json](../../references/release-readiness-gate.schema.json)
- [release-readiness-gate.fixture.json](../../references/release-readiness-gate.fixture.json)
