# Contributing to kano-agent-backlog-skill

This repository is native C++ first. The supported executable contract is the
`kano-backlog` binary reached through `scripts/kob` or `scripts/kano-backlog`.
Do not add Python package entrypoints, pytest oracle tests, runtime `.py`
helpers, or `.pyi` typing stubs back to this repo.

## Development Setup

Prerequisites:

- Git
- Pixi
- CMake and Ninja, normally provided through Pixi/global tooling
- A platform C++ toolchain, such as MSVC Build Tools on Windows

Build and verify:

```bash
cd kano-agent-backlog-skill
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
bash src/shell/test/lint.sh
```

## Code Style

Follow the native layout and naming rules in `src/cpp/`:

- Product behavior belongs in C++ systems under `src/cpp/code/systems/`.
- CLI wiring belongs in `src/cpp/code/apps/kano_backlog_cli/`.
- Shared C++ build/report helper code belongs under `src/cpp/shared/infra/`.
- Shell wrappers should stay thin and should not own core product logic.

## Testing

Native tests live under `src/cpp/code/tests/`. Add focused C++ tests for new
runtime behavior and extend shell gates only when the executable contract
changes.

Before pushing, run:

```bash
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
bash src/shell/test/native-command-coverage.sh --strict
bash src/shell/test/lint.sh
git diff --check
```

## Backlog Discipline

Use backlog IDs directly in commits and worklogs. Create or update items before
substantial code changes, meet the Ready gate before starting, and refresh views
after backlog mutations.

Examples:

```bash
scripts/kob item create --type task --title "Implement native flow" --agent <agent-id> --product kano-agent-backlog-skill
scripts/kob workitem update-state <item-id> --state InProgress --agent <agent-id> --product kano-agent-backlog-skill
scripts/kob view refresh --agent <agent-id> --product kano-agent-backlog-skill
```

## Release Process

Native release validation is documented in `docs/release-checklist.md`.
Python package publishing is retired for this milestone; do not upload wheels or
sdists from this repo.
