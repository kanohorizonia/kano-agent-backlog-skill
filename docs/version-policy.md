# Version Policy

This document records the current release policy for
`kano-agent-backlog-skill`.

## Current Policy

- `0.0.2` is already released.
- `0.0.3` was an untagged historical OSS-readiness planning line.
- `0.0.4` is the current native C++ release target and supersedes the old
  `0.0.3` Python-public package contract.

## Native Executable Contract

- Repo-local execution is native C++ only through `scripts/kob`,
  `scripts/kano-backlog`, or the built `kano-backlog` binary.
- Python package entrypoints, editable installs, pytest oracle tests, and PyPI
  publishing are retired for the `0.0.4` native release line.
- Data formats remain compatible: Markdown backlog files, TOML config, and
  derived SQLite caches continue to be the source-of-truth model.
- Future exact tokenizer or embedding providers must be implemented as native
  adapters before they can enter the executable contract.

## Release Gate Policy

Native releases must prove:

- the native binary version matches `VERSION`,
- quick native smoke tests pass,
- the native runtime gate passes,
- strict native command coverage passes,
- no repo-local `.py` or `.pyi` runtime/type-stub files remain outside generated
  build/cache paths,
- CI and package artifacts do not expose Python package entrypoints.

Historical release notes may still describe the retired Python path for the
release line where it was planned or true. Current docs and automation should
describe the native executable contract.
