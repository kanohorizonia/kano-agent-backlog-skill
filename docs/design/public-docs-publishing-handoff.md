# Public Docs Publishing Handoff

Status: draft handoff contract for KOB public release documentation.

## Goal

The public docs publishing handoff records how evidence-backed release drafts can
move from a release-notes evidence bundle to human-approved public refs. It is a
handoff contract only: it does not publish README, docs site, package metadata,
marketplace/listing text, official release pages, or announcement copy.

The contract exists to stop release notes and release docs from overclaiming.
Public availability must never be claimed unless publication evidence exists for
the destination surface and a human approval marker has passed.

## Handoff Flow

The handoff is ordered:

1. Collect source evidence through a release-notes evidence bundle.
2. Generate draft docs from cited evidence and missing-evidence entries.
3. Review drafts against no-overclaim rules and known limitations.
4. Record human approval checkpoints.
5. Publish to destination surfaces only after approval.
6. Record publication refs, rollback notes, follow-up work, and remaining
   limitations.

Skipping a step blocks public availability claims. Draft output refs are staging
refs only until publication evidence is recorded.

## Destination Surfaces

Each handoff report covers the following surfaces when applicable:

| Surface | Rule |
| --- | --- |
| `readme` | README release claims require approved draft text and a repo commit/ref. |
| `docs_site` | Docs-site release claims require the rendered docs deployment/ref. |
| `package_metadata` | Package-manager metadata requires channel-specific publication evidence. |
| `marketplace_listing` | Marketplace/listing text requires approved listing copy and publication evidence. |
| `official_release_page` | Official release pages require public release/page refs and artifact evidence. |
| `announcement_copy` | Announcement copy must cite approved release notes and known limitations. |

Surfaces may be `planned`, `draft`, `ready_for_review`, `approved`, `published`,
`rolled_back`, `not_applicable`, or `deferred`. Deferred and not-applicable
surfaces must include rationale.

## No-Overclaim Rules

- Do not say artifacts are downloadable until the artifact index and public ref
  exist.
- Do not say package-manager channels are live until channel metadata is
  published and verified.
- Do not turn missing evidence into softer marketing claims.
- Do not remove limitations from public drafts unless the evidence that made
  them true is replaced by newer validation evidence.
- Do not treat a draft public-output ref as live publication evidence.

## Rollback And Follow-Up

Every handoff report records rollback triggers and follow-up policy. If a public
surface overclaims, points at missing artifacts, or omits a known limitation, the
surface must be rolled back or corrected and the release workspace must retain
the incident/follow-up refs. Rollback is not a deletion of evidence; it is a new
evidence entry that explains the correction.

## Integration Points

- `release-notes-evidence-bundle.schema.json` is the source evidence boundary.
- `release-readiness-gate.schema.json` blocks release claims when public docs or
  publication evidence is missing.
- `release-record.schema.json` may reference handoff reports through
  `public_docs_handoff_refs`.
- `releases-directory-contract.schema.json` records handoff reports under the
  release workspace `evidence/` tree and keeps public outputs draft/planned until
  publication review passes.

## Fixture

The schema and example fixture live in:

- [public-docs-publishing-handoff.schema.json](../../references/public-docs-publishing-handoff.schema.json)
- [public-docs-publishing-handoff.fixture.json](../../references/public-docs-publishing-handoff.fixture.json)
