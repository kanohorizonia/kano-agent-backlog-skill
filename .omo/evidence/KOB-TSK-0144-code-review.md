# KOB-TSK-0144 code review

## Verdict

- `codeQualityStatus`: BLOCK
- `recommendation`: REQUEST_CHANGES
- `blockers`:
  1. The live focused Release smoke executable fails the existing 650-item
     metadata-query p95 acceptance assertion. The requested PASS claim is not
     reproducible from the current workspace.
  2. The supplied evidence artifacts are stale and report the pre-fix
     ready-reader failure; they do not provide a successful smoke-test artifact
     for the current patch.

## Scope inspected

- `docs/design/derived-metadata-index.md`
- `src/cpp/code/systems/kano_backlog_ops/index/private/backlog_index.cpp`
- `src/cpp/code/tests/metadata_index_smoke_test.cpp`
- Current uncommitted diff and `.omo/evidence/KOB-TSK-0144/*` artifacts.

## Validation performed

- `git diff --check`: PASS.
- Direct current Release executable invocation:
  `src/cpp/out/bin/windows-ninja-msvc/release/metadata_index_smoke_test.exe`:
  FAIL: `650-item metadata query p95 must remain below 500 ms`.
- Existing artifacts were inspected rather than trusted. Both
  `metadata_index_smoke_test-final.log` and `metadata_index_smoke_test-rerun.log`
  report the earlier failure `simultaneous ready reader must remain ready without
  fallback`; `verification-rerun.txt` also records exit code 1. The build logs
  show compilation/linking but no matching successful test execution.

## Findings

### CRITICAL

None.

### HIGH

1. **Focused acceptance is red.**
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:1527-1529` requires the
   650-item metadata query p95 to remain below 500 ms. The current Release
   executable fails this assertion. This blocks approval because the stated
   success criteria explicitly require the smoke test and its p95 metrics to
   pass. Reproduce after a clean current build and address the regression or
   provide a deterministic environment-backed explanation and accepted
   validation result.

2. **No trustworthy green evidence for the live diff.**
   `.omo/evidence/KOB-TSK-0144/metadata_index_smoke_test-final.log`,
   `metadata_index_smoke_test-rerun.log`, and `verification-rerun.txt` all
   record failures. A build/link log is not a test-pass artifact. Replace these
   with output from the current source revision, including the focused command,
   exit code, and p95 result.

### MEDIUM

1. **The new barrier batch does not deterministically execute the covered
   checkpoint SELECT case.**
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:2360-2392` launches
   concurrent ordinary `query_metadata_index` readers, but it neither forces
   checkpoint persistence nor creates a reader whose endpoint is below an
   already-advanced checkpoint. The changed fallback at
   `backlog_index.cpp:1237-1255` is therefore exercised only opportunistically
   (and may never run: equal endpoints satisfy the `<=` UPDATE). Add a bounded,
   deterministic requested-revision/checkpoint test that proves an
   already-covered endpoint returns unchanged, preserves the larger USN, and
   rejects changed snapshot identity.

2. **Barrier failure paths can hang the smoke test.**
   `src/cpp/code/tests/metadata_index_smoke_test.cpp:2475-2490` blocks the main
   thread at `rebuild_captured` even if `rebuild_metadata` throws before calling
   its hook. Likewise the publication barriers at :2525-2547 have no timeout or
   cancellation path. This turns an implementation failure into a stalled CI
   job rather than a diagnostic test failure. Make the test hooks signal failure
   to the coordinator or use a timeout-capable synchronization mechanism.

### LOW

None.

## Correctness and maintainability notes

The production change is otherwise narrowly scoped. The monotonic UPDATE and
covered-row SELECT retain product, snapshot revision, write revision, proof
kind, root identity, and journal identity predicates; the endpoint-only USN
comparison provides the intended monotonic behavior. The deadline helper caps
SQLite busy waiting at 10 ms and checks the overall deadline before accepting a
result, which remains fail-closed. Skipping invalidation only for
`canonical_write_revision_changed` is consistent with preserving a previously
ready snapshot through the canonical-write-to-publication window. No new
dependency, untyped escape hatch, needless production parsing/normalization,
or production abstraction was introduced.

The full-field/order oracle compares meaningful externally visible item fields,
so it is not a tautological or implementation-constant test. The expanded
parity cases are relevant. No deletion-only test or test that merely validates
the requested removal was found.

## Required skill-perspective check

The required `remove-ai-slops` and `programming` skill files could not be
loaded: the workspace sandbox denied reads from the installed skill directory.
Their stated criteria were applied directly. The production diff does not
violate either perspective. The test diff does violate the programming
perspective on deterministic regression coverage and failure robustness as
described in the MEDIUM findings; it does not exhibit the remove-ai-slops
patterns of tautological, deletion-only, or implementation-mirroring tests.
