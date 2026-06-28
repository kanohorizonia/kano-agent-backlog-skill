# Backboard Policy Context Extension Seam

Status: draft metadata contract for Backboard enterprise policy extension seams.

## Goal

Backboard remains local-first and usable without an enterprise policy provider.
This contract defines policy context refs and capability requirement metadata so
future enterprise routing or policy checks can attach accountable context without
changing the current storage shape.

This seam is advisory metadata only. It does not implement authentication, RBAC,
approval workflow, permission matrix behavior, multi-tenant membership, or an
enterprise UI.

## Policy Context Refs

Policy context refs are bounded canonical refs. A ref identifies a product plus
one or more stable ids such as `policy_context_id`, `item_id`, `uid`,
`topic_id`, `adr_id`, `evidence_id`, `claim_id`, `review_decision_id`, or
`audit_event_id`.

Raw filesystem paths are not policy context identity. Review surfaces and
repo-visible records must expose logical refs only.

A policy context record describes the bounded scope and evidence for a future
policy decision:

- `policy_context_id`: stable policy context id.
- `scope`: bounded scope object with `scope_kind` and optional `scope_ref`.
- `summary`: readable policy context summary.
- `evidence_refs`: logical refs that explain the policy context.

## Capability Requirement Metadata

A capability requirement describes what a future enterprise policy provider may
need to evaluate for an action or surface. It is not a grant, deny decision, or
runtime permission check.

Required shape:

- `requirement_id`: stable requirement id.
- `required_capabilities`: capability names such as
  `backboard.review_decision.submit`.
- `affected_action`: action being described.
- `affected_surface`: Backboard surface being described.
- `evidence_refs`: logical refs supporting the requirement.
- `rationale`: human-readable reason for recording the requirement.

Optional shape:

- `actor_alias`: repo-visible actor alias when the requirement is scoped to a
  role-like alias.
- `policy_context_ref`: logical ref to a policy context.
- `subject_ref`: logical ref for the affected item, ADR, work order, evidence,
  or other bounded subject.

Actor aliases follow the repo-visible alias rules from
[Actor Alias And Assignment Policy](actor-alias-and-assignment-policy.md).

## Local-First Omitted Fields

Single-user mode remains valid when policy context and capability requirement
fields are omitted. A local Backboard may render and submit existing review data
without an enterprise policy provider, capability resolver, auth provider, or
tenant membership service.

Missing enterprise policy provider metadata must not block local Backboard
operation in this seam. At most, a future reader may display a gap or advisory
notice that policy context is unavailable.

## Non-Goals

- No authentication provider.
- No RBAC enforcement.
- No approval workflows.
- No permission matrix behavior.
- No multi-tenant membership.
- No enterprise UI.
- No Ark Console coupling.
- No broad item migration.

## Fixture

The schema and example fixture live in:

- [backboard-policy-context-extension-seam.schema.json](../../references/backboard-policy-context-extension-seam.schema.json)
- [backboard-policy-context-extension-seam.fixture.json](../../references/backboard-policy-context-extension-seam.fixture.json)
