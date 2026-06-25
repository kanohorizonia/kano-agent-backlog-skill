# Release Record Schema

Status: draft schema contract for KOB release records.

## Goal

Release records are bounded product release snapshots. They describe planned,
assembling, validating, released, or archived release scope without replacing
the backlog hierarchy, without inferring public publication, and without
starting a release workflow.

Release membership is orthogonal to parent and child hierarchy. Adding an item
to a release must not reparent that item, and removing an item from a release
must not change its parent.

## Release Record Fields

Each release record includes:

| Field | Meaning |
| --- | --- |
| `release_id` | Stable release record id for the product release line. |
| `target_products` | Product refs covered by the release scope. |
| `lifecycle_state` | Release lifecycle state from the taxonomy below. |
| `goals` | Release goals and optional links to Version Goal Ledger goals. |
| `included_items` | Items intentionally included in this release scope. |
| `deferred_items` | Items reviewed for the release but explicitly moved out. |
| `validation_refs` | Validation evidence refs for the release state claims. |
| `dogfood_refs` | Internal dogfood refs that support release confidence. |
| `limitations` | Known gaps, constraints, or caveats that remain true. |
| `release_notes_artifact_refs` | Draft or published release note artifact refs. |
| `public_docs_refs` | Public doc refs or planned publication targets. |

## Release Lifecycle Taxonomy

| State | Meaning |
| --- | --- |
| `planned` | The release line exists, but scoped work is still provisional. |
| `assembling` | Included items and release artifacts are being assembled. |
| `validating` | Scope is assembled and evidence is being reviewed. |
| `released` | The release is accepted as released in the release record. |
| `archived` | The release record is retained for history and no longer active. |

## Rules

- Release membership is not a parent axis. Included or deferred items keep their
  existing parent and child relationships.
- Release refs must stay bounded. Prefer `product`, `item_id`, `uid`,
  `topic_id`, `adr_id`, `evidence_id`, `artifact_id`, `doc_id`, `public_url`,
  and `reason` style fields instead of raw local filesystem paths.
- `released` in this schema records release state, not an automatic public
  publication claim. Public release surfaces still require explicit human review
  and publication.
- KOB-TSK-0065 Version Goal Ledger remains the roadmap contract. Release goals
  may reference ledger goals, but they do not replace the ledger or its status
  model.
- KOA-TSK-0215 and KOA-TSK-0216 define the evidence lineage around the release
  record and release packet model. This schema records release scope and bounded
  evidence refs, while human publication remains a separate gate.
- Draft examples may point to placeholder public URLs for shape only. They must
  not be interpreted as live public release content.

## Fixture

The schema and example fixture live in:

- [release-record.schema.json](../../references/release-record.schema.json)
- [release-record.fixture.json](../../references/release-record.fixture.json)
- [release-record-v0.2.0.fixture.json](../../references/release-record-v0.2.0.fixture.json)
