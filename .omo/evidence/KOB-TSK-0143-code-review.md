# Code Review: KOB-TSK-0143

## Result

- codeQualityStatus: BLOCK
- recommendation: REQUEST_CHANGES
- scope: only the seven task-owned files named in the review request

## Skill perspective check

Ran: `omo:programming` and `omo:remove-ai-slops` were loaded before judging
maintainability and test relevance. The C++ change does not use the typed
languages that trigger the programming skill's language gate, but its shared
requirements apply here: parse/validate at the boundary, reject malformed data,
and test observable contract behavior. The diff violates that perspective by
implementing a partial, unvalidated JSON boundary. It also violates the
remove-ai-slops perspective: the new test-side adapter and schema have duplicate
and incomplete validation instead of one contract validator; the happy-path
fixtures mirror the implementation and leave required rejection behavior
uncovered.

## Findings

### CRITICAL

None.

### HIGH

1. The advertised JSON Schema is never validated, so it cannot enforce the
   public contract or fixture shape. `parse_json_fixture` only parses JSON at
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:72-85`; the later checks
   inspect a handful of constants and execute scenarios without applying
   `references/koa-metadata-index-consumer.schema.json`. Consequently an
   upstream item missing `id`, `uid`, `type`, `state`, `title`, `parent`, or
   `updated` passes the only item check at `:234-243` and is projected with null
   fields, contrary to the canonical required fields in the schema at
   `references/koa-metadata-index-consumer.schema.json:184-241` and the
   documented response at `docs/design/koa-metadata-index-consumer-contract.md:82-90`.
   This fails the acceptance criterion for schema validation and canonical-field
   parity, while giving a green smoke test.

2. The documented 16-token request bound is neither expressible in the schema
   nor checked by the executable fixture adapter. The contract requires it at
   `docs/design/koa-metadata-index-consumer-contract.md:47-50`, but item-search
   validation checks only 512 characters at
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:199-208`; the fixture has
   no 17-token rejection case (`references/koa-metadata-index-consumer.fixture.json:181-254`).
   A 17-token query under 512 bytes is accepted rather than failing closed,
   violating the bounded-request contract.

3. Bounded diagnostic and revision fields are blindly copied from the upstream
   envelope. `matched_count`, `scanned_count`, `elapsed_ms`, `cache_status`,
   `product_revision`, `index_revision`, and `stale_reason` are assigned without
   type, range, enum, or length checks at
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:222-233` and `:265-278`.
   The schema defines the corresponding bounds/enums (for example diagnostics
   at `references/koa-metadata-index-consumer.schema.json:121-182` and status
   entries at `:330-387`), but the test does not apply it. A producer can thus
   make the consumer emit unbounded counts, strings, or invalid cache status,
   violating the ticket's bounded diagnostics requirement and the document's
   promised response limits.

### MEDIUM

1. The privacy test is a narrow name blacklist rather than an allowlist or
   schema check. `expect_no_forbidden_consumer_fields` at
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:117-143` misses equivalent
   producer-private names such as `path`, `database_error`, and proof fields.
   The current projection happens to hide them, but a future projection change
   can leak them while the test remains green. Validate the complete adapter
   response against an allowlisted response schema instead.

2. `consumer_error` returns raw request `operation` and `product` before their
   bounds are validated (`src/cpp/code/tests/metadata_index_smoke_test.cpp:145-164`,
   especially `:153-154`). An unsupported consumer version with an arbitrarily
   large product/operation therefore creates an unbounded error response. This
   conflicts with the bounded-error requirement at
   `docs/design/koa-metadata-index-consumer-contract.md:134-147`.

### LOW

None.

## Validation inspected

- `git diff --check`: no task-owned whitespace error observed (warnings were
  from unrelated concurrent files).
- Independently ran the existing Release executable:
  `src/cpp/out/bin/windows-ninja-msvc/release/metadata_index_smoke_test.exe`;
  PASS (reported exact 28.1989 ms, metadata 341.697 ms, token 412.036 ms,
  revision 19.4884 ms, unchanged status 17.6626 ms).
- The pass does not resolve the findings because the executable tests only the
  hand-authored happy/error fixture cases and does not run JSON Schema
  validation or the untested invalid-boundary cases.

## Scope and residual risk

No scope drift was found in the task-owned documentation/reference files.
The test-side adapter is task-scoped but should not be treated as a trustworthy
KOA integration until it validates the published schema and failure cases.

## Re-review update (latest diff)

- codeQualityStatus: BLOCK
- recommendation: REQUEST_CHANGES

The previous three HIGH findings are resolved: request token/type/unknown-field bounds, upstream query/status/item bounds, and response-envelope checks are now enforced and exercised by mutations at `src/cpp/code/tests/metadata_index_smoke_test.cpp:682-717`.

### Remaining HIGH

Freshness flags are still validated independently rather than as a contract state. `is_valid_query_diagnostics` accepts any combination of `index_used`, `index_status`, and `fallback_scan` at `src/cpp/code/tests/metadata_index_smoke_test.cpp:173-205`; the adapter copies `index_status` and `fallback_scan` into the response at `:493-503`. An upstream envelope with `index_used=false`, `index_status=ready`, and `fallback_scan=false` is accepted and emitted as apparently indexed-ready output. This contradicts the documented invariant that ready/no-fallback is indexed and fallback is never labelled indexed/unchanged (`docs/design/koa-metadata-index-consumer-contract.md:107-122`). Reject inconsistent query diagnostics (and analogous status combinations) and add a mutation proving rejection.

### Independent validation

- `git diff --check` for task-owned paths: PASS.
- Current Release `metadata_index_smoke_test.exe`: PASS (exact 34.2312 ms, metadata 368.562 ms, token 338.703 ms, revision 23.0376 ms, unchanged status 17.1606 ms).
- Required skill-perspective check ran (`omo:programming`, `omo:remove-ai-slops`). No additional slop violation found in the latest diff.

## Re-review update (freshness invariant fix)

- codeQualityStatus: BLOCK
- recommendation: REQUEST_CHANGES

The prior freshness blocker is resolved: query `ready` now requires indexed/no-fallback/null-reason, non-ready requires canonical fallback/nonempty reason, and the mutations cover ready-without-index-use, contradictory unchanged, status fallback mislabel, and requested-revision echo mismatch.

### Remaining HIGH

`is_valid_status_entry` only applies stale-reason coherence in its `status == "unchanged"` arm (`src/cpp/code/tests/metadata_index_smoke_test.cpp:324-332`). The non-unchanged arm checks only `!unchanged`; it permits `status` `stale`, `missing`, or `corrupt` with `stale_reason: null` or `""`, and it also permits `ready` with a non-null stale reason. The adapter then emits those contradictory diagnostics unchanged (`:562-572`). This conflicts with the documented requirement to preserve the fail-closed distinction for unsupported, missing, and corrupt revision proof (`docs/design/koa-metadata-index-consumer-contract.md:124-132`). Enforce the status/reason relationship and add mutation coverage for a stale/missing/corrupt status missing its reason (and, ideally, ready carrying one).

### Independent validation

- `git diff --check` for the task-owned scope: PASS.
- Current Release `metadata_index_smoke_test.exe`: PASS (exact 35.4282 ms, metadata 342.041 ms, token 331.861 ms, revision 22.9154 ms, unchanged status 18.1943 ms).
- Required `programming` and `remove-ai-slops` review perspectives were already loaded for this review; no new slop-specific finding.

## Final re-review update

- codeQualityStatus: CLEAR
- recommendation: APPROVE
- blockers: none

Verified the current query and status conditional invariants in both executable validation and JSON Schema. Query `ready` requires indexed/no-fallback/null-reason, while non-ready requires fallback/nonempty reason. Status disallows fallback and enforces separate ready, unchanged, and stale/missing/corrupt flag/reason states at `src/cpp/code/tests/metadata_index_smoke_test.cpp:209-239,275-336`; the schema mirrors these conditionals at `references/koa-metadata-index-consumer.schema.json:182-208,470-514`. Negative tests cover stale-without-reason and ready-with-stale-reason at `metadata_index_smoke_test.cpp:760-777`.

Final independent validation: task-owned `git diff --check` PASS; Release `metadata_index_smoke_test.exe` PASS (exact 35.1499 ms, metadata 365.089 ms, token 358.186 ms, revision 22.6108 ms, unchanged status 20.8972 ms). Skill-perspective check (`omo:programming`, `omo:remove-ai-slops`) remains clean for the final diff.
