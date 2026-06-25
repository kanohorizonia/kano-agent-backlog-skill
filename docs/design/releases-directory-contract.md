# Releases Directory Contract

Status: draft schema contract for KOB release workspace manifests.

## Goal

The releases workspace is a release and evidence axis under
`_kano/backlog/releases/<version>/`. It is a sibling to `products/` and
`topics/`, not a backlog hierarchy parent.

Release workspaces collect bounded release metadata, scope views, evidence refs,
draft notes, source artifact refs, and human verification markers without
reparenting included backlog items and without claiming live public
publication.

## Workspace Manifest Fields

Each workspace manifest includes:

| Field | Meaning |
| --- | --- |
| `schema` | Contract marker, must be `kob.release.workspace.v1`. |
| `workspace_root` | Repo-relative workspace root under `_kano/backlog/releases/<version>/`. |
| `release_id` | Stable release workspace id for the release line. |
| `version_label` | Release version label for the workspace, such as `v0.1.0`. |
| `product` | Product id covered by the workspace manifest. |
| `lifecycle_state` | Release lifecycle state aligned with the release record contract. |
| `summary` | Short bounded summary of the draft release scope. |
| `release_record_refs` | Bounded refs that connect the workspace to release-record semantics. |
| `workspace_entries` | Explicit allowed files and directories in the workspace. |
| `goals` | Release goals and optional links to Version Goal Ledger goals. |
| `included_items` | Items intentionally included in the release scope view. |
| `deferred_items` | Items reviewed for the release but moved out of scope. |
| `evidence_refs` | Evidence refs used to support release state, scope claims, and release-note draft claims. |
| `source_artifact_refs` | Repo-relative source artifact refs, such as CI indexes or packet drafts. |
| `notes_draft_refs` | Draft release note refs generated from release-notes evidence bundles; they remain non-public until human review passes. |
| `public_output_refs` | Planned public output refs. These stay draft or planned until publication review passes. |
| `human_verification` | Human review markers for scope, evidence, and publication gates. |

## Allowed Workspace Layout

The manifest is the contract for the allowed workspace entries. The required
entry set is:

| Entry | Kind | Purpose |
| --- | --- | --- |
| `release.json` | file | Machine-readable release workspace manifest. |
| `goals.md` | file | Human-readable release goals summary. |
| `included-items.md` | file | Human-readable included scope list. |
| `deferred-items.md` | file | Human-readable deferred scope list. |
| `evidence/` | directory | Evidence indexes and evidence-adjacent records. |
| `source-artifacts/` | directory | Source artifact indexes and draft packet lineage. |
| `notes-drafts/` | directory | Non-public release note drafts. |
| `public-output/` | directory | Planned public output drafts and staging refs. |
| `verification/` | directory | Human verification markers and review notes. |

The manifest should name these entries explicitly through `workspace_entries` so
tools can validate the workspace without scanning unknown content.

## Release Notes Evidence Collection

Release-note drafting starts from an evidence bundle, not from memory. The bundle
is a bounded artifact under the release workspace evidence or source-artifacts
tree and is described by
[release-notes-evidence-bundle.schema.json](../../references/release-notes-evidence-bundle.schema.json).

The collection workflow is:

1. Collect source inputs: included items, relevant commits, validation refs,
   dogfood refs, known limitations, planned public docs targets, and human
   verification markers.
2. Record missing evidence for every attractive but unsupported release-note
   claim.
3. Generate draft release notes only from cited source inputs and missing
   evidence entries.
4. Save draft notes under `notes-drafts/` and link them through
   `notes_draft_refs`.
5. Keep public-output refs planned or draft until the public docs handoff and
   human publication review pass.

AI-authored draft notes are allowed as draft artifacts, but every section must
carry source refs or explicit missing-evidence refs. Human approval is required
before draft notes are used as public-facing release notes. `KOB-TSK-0093` owns
the public docs publishing handoff; this contract only defines evidence
collection and draft lineage.

## Rules

- The release workspace is not a parent axis. Included or deferred items keep
  their existing parent and child relationships under `products/` or `topics/`.
- `workspace_root` and every repo-relative path field must stay bounded to the
  repository. Raw absolute paths, drive letters, UNC paths, and host-local path
  leaks are forbidden.
- Bounded refs should use ids and repo-relative identifiers such as `product`,
  `item_id`, `goal_id`, `evidence_id`, `artifact_id`, `doc_id`, and
  `repo_relpath`.
- `release_id` and `version_label` identify the workspace metadata. They should
  align with the release record for the same release line.
- `lifecycle_state` may mirror the release record state, but the workspace does
  not replace `release-record.schema.json` and does not redefine release
  acceptance semantics.
- `public_output_refs` are planned or draft refs until the human publication
  marker passes. They must not be read as proof of live public publication.
- `notes_draft_refs` must trace back to release-notes evidence bundles. Missing
  evidence should remain visible in drafts instead of being converted into
  unsupported release claims.
- KOA-TSK-0215 packet concepts are useful lineage inputs, but this contract does
  not migrate HorizonPlugin, create live public content, or start a publishing
  workflow.
- Draft examples may show public-output targets by id and repo-relative draft
  location. They are shape examples only, not live releases.

## Fixture

The schema and example fixture live in:

- [releases-directory-contract.schema.json](../../references/releases-directory-contract.schema.json)
- [release-notes-evidence-bundle.schema.json](../../references/release-notes-evidence-bundle.schema.json)
- [release-notes-evidence-bundle.fixture.json](../../references/release-notes-evidence-bundle.fixture.json)
- [releases-directory-contract.fixture.json](../../references/releases-directory-contract.fixture.json)
- [releases-directory-contract-v0.2.0.fixture.json](../../references/releases-directory-contract-v0.2.0.fixture.json)
