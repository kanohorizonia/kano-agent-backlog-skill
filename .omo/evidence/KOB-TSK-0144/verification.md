# KOB-TSK-0144 current verification

Captured: `2026-08-31T22:46:44+08:00`

## Scope identity

- Git HEAD: `de2a44f4b6d3cb66e490b40afa6f25599521bfd7`
- Release executable size: `2204672` bytes
- Release executable write time: `2026-08-31 22:30:47 +08:00`
- Exact source and executable hashes: `source-hashes.sha256`
- The dirty prefix-migration source and test are unrelated and excluded from
  KOB-TSK-0144 review scope.

## Verification results

- `pixi run build-release`: PASS, exit code 0. The current smoke source was
  compiled and the focused Release executable was linked.
- Direct focused Release execution: PASS three times after explicit checks for
  external CMake, CTest, Ninja, MSVC compiler/linker, and relevant smoke-test
  processes. Exact output is preserved in the three run artifacts.
- `GIT_MASTER=1 git diff --check`: PASS, exit code 0.

All three runs satisfy the unchanged acceptance thresholds:

| Metric | Threshold | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: | ---: |
| exact p95 | `< 100 ms` | 18.1552 | 17.4602 | 16.4516 |
| metadata p95 | `< 500 ms` | 156.345 | 155.408 | 162.915 |
| token p95 | `< 2000 ms` | 142.116 | 140.317 | 151.718 |
| revision p95 | `< 100 ms` | 11.0355 | 9.8167 | 9.2803 |
| unchanged-status p95 | `< 100 ms` | 6.5451 | 5.912 | 6.0453 |

## Regression coverage

- Six bounded condition-variable-coordinated readers with cancellation,
  exception propagation, and guaranteed joins.
- Readers across the old-ready/new-ready publication boundary and two
  deterministic replacement publications.
- Cross-product exact, metadata-filtered, token, all-item, portable, deletion,
  fallback, and ordering parity against the canonical query oracle.
- Mandatory checkpoint contention fails closed within the absolute deadline.
- A deterministic already-covered endpoint remains unchanged and preserves the
  larger durable USN.
- Changed snapshot identity is rejected rather than accepting a checkpoint from
  another publication.
- Stale cleanup cannot invalidate a replacement ready snapshot.

## Known tooling limitation

clangd diagnostics are not authoritative in this Windows workspace because the
configured clangd process cannot resolve the MSVC standard-library headers (for
example, `algorithm` is reported missing). The canonical MSVC Release build is
the compiler gate and passed. The aggregate quick-test inventory drift is an
unrelated CLI11 count mismatch and is not claimed as KOB-TSK-0144 evidence.
