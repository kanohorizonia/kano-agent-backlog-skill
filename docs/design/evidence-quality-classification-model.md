# Evidence Quality Classification Model

Status: draft schema contract for Backboard evidence review.

## Goal

Evidence quality classification helps reviewers see whether a claim is backed by
durable records. It is not a scoring oracle and must link back to raw evidence so
humans can challenge the classification.

## States

| State | Meaning |
| --- | --- |
| `strong` | Evidence directly supports the claim and is current. |
| `weak` | Evidence exists but is indirect, incomplete, or narrow. |
| `missing` | No durable evidence is attached or discoverable. |
| `stale` | Evidence exists but predates the relevant change or target. |
| `unclear` | Evidence exists but does not clearly prove or disprove the claim. |
| `contradicting` | Evidence conflicts with the claim or with another source. |

## Inputs

Classification inputs can include commits, validation output, artifacts, worklog
entries, dogfood records, review notes, and falsifier data. Heuristics are
diagnostics only; they must not be presented as proof.

## Human Wording

Backboard should phrase evidence quality as review guidance:

- `strong`: evidence is sufficient for review unless new risks are known.
- `weak`: request better evidence before accepting high-risk claims.
- `missing`: do not treat the claim as proven.
- `stale`: re-run or refresh evidence.
- `unclear`: ask for interpretation or a narrower claim.
- `contradicting`: resolve the conflict before Done.

## Non-Goals

- No hidden model score.
- No replacement for raw artifacts or worklog evidence.
- No automatic state transitions or agent execution.
- No Ark Console admission decision.

## Fixture

The schema and example fixture live in:

- [evidence-quality-classification.schema.json](../../references/evidence-quality-classification.schema.json)
- [evidence-quality-classification.fixture.json](../../references/evidence-quality-classification.fixture.json)
