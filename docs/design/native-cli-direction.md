# Native CLI Direction

Status: accepted as the current executable contract for the native migration
milestone.

## Context

The project began with a Python implementation because it was fast to iterate,
easy to inspect, and had broad library support. That was a reasonable first
implementation while the backlog model, command shape, and data formats were
still settling.

Agentic usage changes the tradeoff. Agents invoke the CLI repeatedly while
planning, validating, repairing, and re-checking work. In that environment,
startup cost, dependency drift, import failures, and Python package installation
friction become operational risks.

## Direction

The repo-local executable surface is now native C++:

- `scripts/kob` and `scripts/kano-backlog` require a built native binary,
- command behavior lives in native C++ systems/apps under `src/cpp/`,
- shell wrappers stay thin and do not own product behavior,
- Python package entrypoints and pytest oracle tests are retired,
- optional exact tokenizer or embedding behavior must arrive through future
  native provider adapters.

## What This Means For Releases

This native migration milestone is distinct from the old `0.0.3` Python-public
release contract. Historical release notes may still describe the Python package
path for releases where that was true, but current automation and repo-local
usage target the native executable contract.

## Verification

The native direction is enforced by:

- native build and smoke tests,
- strict native command coverage,
- runtime gates that reject launcher fallback and repo-local `.py`/`.pyi`
  files,
- release workflows that build and test the native binary instead of running
  Python package gates.
