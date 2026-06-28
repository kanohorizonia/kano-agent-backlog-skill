# Backboard Enterprise Envelope Seams

Status: draft metadata contract for Backboard enterprise extension seams.

## Goal

Backboard remains local-first and single-user friendly, but its persisted review
metadata needs stable places for future enterprise systems to attach
accountability, evidence, policy context, and audit signals. This contract
defines the claim, lease, review-decision, and audit event envelopes without
implementing authentication, authorization, approval quorum, or runtime locks.

## Canonical Ref Boundary

All envelope links use bounded logical refs. A ref identifies a product plus one
or more stable ids such as `item_id`, `uid`, `adr_id`, `evidence_id`,
`claim_id`, `lease_id`, `review_decision_id`, `audit_event_id`, or
`policy_context_id`.

Raw filesystem paths are not envelope identity. Implementations may keep private
storage paths internally, but review surfaces and repo-visible envelope records
must expose logical refs only.

Actor fields use the repo-visible alias rules from
[Actor Alias And Assignment Policy](actor-alias-and-assignment-policy.md). The
repo stores aliases such as `koa`, `codex`, `maintainer`, or `reviewer-koa`; any
mapping to a person, account, credential, or directory subject belongs outside
tracked backlog files.

## Claim Records

A claim record represents an actor alias taking accountability for a bounded
subject. It is not a permission grant and it does not lock the subject.

Required shape:

- `claim_id`: stable record id.
- `owner_actor_alias`: repo-visible actor alias that owns the claim.
- `claimed_subject_ref`: logical ref for the item, ADR, evidence, claim, or
  policy context being claimed.
- `scope`: bounded scope object with `scope_kind` and optional `scope_ref`.
- `created_at` / `updated_at`: UTC timestamps.
- `status`: `active`, `released`, `expired`, or `superseded`.
- `evidence_refs`: logical refs that explain or support the claim.

Optional lease fields may add `expires_at` and a nested lease envelope. Expiry is
advisory metadata until a future workflow chooses to enforce it outside this
contract.

## Lease Lifecycle

A lease records lifecycle state for a claim-like reservation without enforcing
runtime locks. The allowed lease states are:

| State | Meaning |
| --- | --- |
| `active` | Lease is currently asserted by the owner alias. |
| `released` | Owner alias intentionally released the lease. |
| `expired` | Lease expiry time passed or was marked expired. |
| `superseded` | A later lease replaces this lease. |

Backboard may display lease state as context, but KOB must not treat it as a
write lock or authorization check in this slice.

## Review Decision Envelope

Review decisions record accountable human or operator judgment. They are
separate from detector suggestions and from KOB state-transition enforcement.

Required shape:

- `decision_id`: stable decision record id.
- `actor_alias`: alias that made or recorded the decision.
- `decision_status`: one of `approved`, `rejected`, `request_changes`,
  `deferred`, `accepted_risk`, `dismissed`, or `commented`.
- `rationale`: human-readable rationale.
- `evidence_refs`: logical evidence refs used by the decision.
- `related_refs`: logical refs for affected items, work orders, ADRs, or
  evidence records.

The existing Backboard Review Inbox can continue storing current
`review_decision` records. This envelope defines the durable extension shape for
future fields without requiring personal names or emails.

## Audit Event Envelope

Audit events are append-only observations of actions, not permission decisions.

Required shape:

- `event_id`: stable event id.
- `actor_alias`: actor alias associated with the action.
- `action_kind`: machine-readable action name such as `claim.created`,
  `lease.released`, or `review_decision.submitted`.
- `target_ref`: logical ref for the target.
- `timestamp`: UTC timestamp.
- `evidence_refs`: logical refs that support the event.
- `policy_context_ref`: optional logical ref to policy context metadata.

Policy context refs are metadata hooks. They do not imply RBAC, tenant
membership, approval quorum, or permission enforcement.

## Local-First Behavior

Single-user/local-first operation works with aliases only. A local Backboard can
record `koa`, `codex`, or `maintainer` as actor aliases without any auth
provider. Missing enterprise identity mapping is not an error for this seam.

## Non-Goals

- No authentication provider.
- No RBAC or permission checks.
- No multi-tenant membership model.
- No approval quorum.
- No runtime lock enforcement.
- No requirement to store personal names, emails, account ids, or credentials in
  repo files.
- No Ark Console dependency.

## Fixture

The schema and example fixture live in:

- [backboard-enterprise-envelope-seams.schema.json](../../references/backboard-enterprise-envelope-seams.schema.json)
- [backboard-enterprise-envelope-seams.fixture.json](../../references/backboard-enterprise-envelope-seams.fixture.json)
