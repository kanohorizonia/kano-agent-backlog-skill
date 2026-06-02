# Version policy

This document records the current public release policy for `kano-agent-backlog-skill`.

## Current policy

- `0.0.2` is already released.
- `0.0.3` is the current public OSS-readiness release target.
- `0.0.3` focuses on public-facing cleanup: README/docs coherence, GitHub Pages, package metadata, release hygiene, Codex for OSS positioning, and validation cleanup.

## Native implementation direction

- The current public release still includes the Python implementation and Python packaging path.
- Native C++ CLI work is a future direction intended to reduce runtime dependencies and improve distribution, CI, and repeated agent CLI ergonomics.
- Native implementation work should not be presented as the stable public CLI until tests and docs validate it at the same level as the Python path.

## Future milestone framing

- `0.1.0` and broader native migration work remain future/internal planning until validated.
- References to `0.1.0` in planning notes should be read as forward-looking, not as the current public release target.
