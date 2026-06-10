# Native executable coverage inventory

Status: native-only executable contract for the Python-to-C++ migration milestone.

## Purpose

This inventory records the command surfaces that replaced the retired Python CLI,
runtime operations, tests, and release helpers. The file name is retained for
migration history. The supported executable contract is now native C++ only.

## Contract

- `scripts/kob` and `scripts/kano-backlog` route to the native binary.
- `src/python`, `tests`, `test_vcs.py`, `pyproject.toml`, and package metadata are
  removed from the supported executable surface.
- Runtime gates reject repo-local `.py` and `.pyi` files outside generated build
  output and local tool/cache directories.
- Python-only in-process providers such as `tiktoken`, HuggingFace
  `sentence-transformers`, and Python package publishing are retired from the
  executable contract until a native provider is selected.
- Shell remains only for thin launchers, native build/test orchestration, docs
  file movement, and compatibility entrypoints that invoke native binaries or
  standard platform tools.

## Top-level native command coverage

| Command | Status | Evidence |
| --- | --- | --- |
| `admin` | native covered | Native help gate plus smoke coverage for `init`, `sync-sequences`, sandbox, persona, release, items, ADR, schema, validate, links, demo, and meta routed commands. |
| `workitem` / `item` | native covered | Native help gate plus smoke coverage for create, Ready fields, state update, worklog, trash, parent, decision, and artifact flows. |
| `state`, `worklog`, `view`, `topic`, `workset` | native covered | Native help gate plus smoke coverage for state transitions, worklog append, view refresh, topic lifecycle/materials/snapshot/workset-state commands, and workset lifecycle commands. |
| `evidence`, `assumptions` | native covered | Native file-backed evidence CRUD/summary and assumptions list/generate surfaces are covered by help gates and representative smoke workflows. |
| `config`, `schema`, `validate`, `links`, `adr`, `changelog`, `demo`, `orphan`, `meta`, `snapshot` | native covered | Native help gate covers command surfaces; smoke coverage exercises representative config, validation, links, ADR, changelog, demo, orphan, meta, and snapshot behavior. |
| `repo-hygiene` | native covered | Native Git-index hygiene check/fix covers executable bits, LF index issues, and archive-safe shell audit discovery. |
| `export` | native covered | Native Git archive export writes a single tar archive with optional archive-safe hygiene validation. |
| `inspect` | native covered | Native `inspect health` produces deterministic JSON/Markdown trust-gap reports over item files and native evidence stores. |
| `benchmark` | native covered | Native `benchmark run` supports deterministic chunk-only/noop provider reports without Python runtime adapters. |
| `chunks` | native covered | Native backlog/repo chunks DB build/query uses SQLite FTS5 and canonical DB naming. |
| `tokenizer` | native covered | Native heuristic tokenizer diagnostics/counting, stateless cache reporting, accuracy smoke, config migration, compare, install-policy guidance, telemetry status/export/clear, monitor, and alerts are available. |
| `embedding` | native covered | Native build/query/status supports noop/heuristic/local FTS behavior and no longer routes to Python providers. |
| `search` | native covered | Native query/hybrid use local FTS over backlog or repo chunks. Vector reranking remains a future native-provider enhancement, not a Python fallback. |

## Native long-text file input

Native workitem write paths support file-backed long text without requiring large
shell-quoted arguments:

- `workitem set-ready`: `--context-file`, `--goal-file`, `--approach-file`,
  `--acceptance-criteria-file`, `--risks-file`
- `workitem update-state`: `--message-file`
- `workitem add-decision`: `--decision-file`
- `state transition`: `--message-file`
- `worklog append`: `--message-file`

By default these options read the file and leave it in place. Passing
`--consume-input-files` deletes the referenced input files only after a
successful write, and only when the files are under `~/.kano/tmp/backlog` or the
test/automation override root in `KANO_BACKLOG_TEXT_TMP`.

## Topic coverage

| Topic command group | Status | Evidence |
| --- | --- | --- |
| `create`, `add`, `list`, `switch`, `distill`, `status`, `export-context` | native covered | Native topic lifecycle and export commands are covered by help gates and smoke tests. `topic create --template` uses native JSON templates. |
| `template list/show/validate` | native covered | Native template JSON discovery covers custom `_config/templates` and built-in `templates/*/template.json`, including file/variable validation. |
| `snapshot create/list/restore/cleanup` | native covered | Native topic-local snapshot JSON operations are covered by help gates and representative smoke workflows. |
| `pin`, `add-snippet`, `decision-audit`, `close`, `cleanup`, `add-reference`, `remove-reference` | native covered | Native manifest/JSON operations cover pinned docs, SHA-256 snippet refs, cached text snapshots, audit, lifecycle, cleanup, and relationship updates. |
| `list-active`, `show-state`, `migrate`, `cleanup-legacy`, `migrate-filenames` | native covered | Native shared topic state and legacy marker migration commands are covered by help gates and smoke workflows. |
| `sync-opencode-plan`, `resolve-opencode-plan` | native covered | Native Oh My OpenCode `.sisyphus` compatibility commands cover plan sync, provider resolution, active-topic fallback, and `boulder.json` metadata. |
| `split`, `merge` | native covered | Native manifest operations cover dry-run plans, item redistribution/merge, related topic rewrites, source deletion support, and shared workset state updates. |

## Tokenizer coverage

| Tokenizer command group | Status | Evidence |
| --- | --- | --- |
| `test`, `count`, `benchmark`, `config`, `create-example`, `validate`, `status`, `dependencies`, `recommend`, `models`, `list-models`, `env`, `env-vars`, `health`, `health-check`, `diagnose`, `adapter-status` | native covered | Native heuristic tokenizer behavior is available with deterministic JSON/Markdown diagnostics. |
| `cache-stats`, `cache-clear` | native covered | Native tokenizer is stateless; commands report zero entries and successful empty clear operations without global Python cache state. |
| `accuracy` | native covered | Runs deterministic native heuristic self-check samples and can write a JSON report. It is not an exact tiktoken/HuggingFace oracle. |
| `compare` | native covered | Compares native heuristic behavior and reports Python-only adapters as excluded from the native executable contract. |
| `migrate` | native covered | Reads JSON or simple TOML tokenizer config and writes native TOML defaults, preserving excluded previous adapters as `previous_adapter`. |
| `install`, `install-guide` | native covered | Reports native provider installation policy; the native CLI does not install Python packages or in-process Python providers. |
| `telemetry`, `telemetry-export`, `telemetry-clear`, `monitor`, `alerts` | native covered | Reports deterministic disabled/empty native telemetry state and can export an empty telemetry JSON payload. |

## Harness

Run the native coverage report:

```bash
bash src/shell/test/native-command-coverage.sh
```

Run strict native coverage mode:

```bash
bash src/shell/test/native-command-coverage.sh --strict
```

The harness calls the native binary directly, so it is independent of
`scripts/kob` launcher behavior. Strict mode fails if any command listed in the
coverage manifest lacks a native help surface.

## Native runtime gate

Normal launcher execution now requires a version-matching native binary.
`src/shell/core/kano-backlog` no longer contains a Python fallback, and the
former Python oracle path has been removed.

Run the runtime gate:

```bash
bash src/shell/test/native-runtime-gate.sh
```

The gate verifies:

- native binary version matches `VERSION`,
- launcher has no Python fallback code,
- removed Python source/package/test directories are absent,
- no repo-local `.py` or `.pyi` files remain outside generated build/cache paths,
- default Pixi manifest and lock have no Python runtime/package records,
- Windows assert/error-dialog suppression is centralized for native automation,
- repo-local `scripts/kob` can execute the native binary.

## Test and release entrypoints

- `src/shell/test/quick-test.sh` runs native C++ smoke tests through
  `native-test.sh`.
- `src/shell/test/run-all-tests.sh` runs native tests plus
  `native-runtime-gate.sh`.
- `src/shell/test/lint.sh` runs native-contract checks instead of Python
  ruff/black/isort/mypy.
- `.github/workflows/release-gates.yml` no longer installs Python or runs Python
  pytest/package gates; it builds the native CLI, runs native quick tests,
  verifies the runtime gate, and runs native `admin release check --phase
  phase1`.
- `.github/workflows/pages.yml` no longer installs Python docs dependencies. The
  docs pipeline writes native CLI/API overview content and a native API
  placeholder; MkDocs/mkdocstrings helper scripts were removed.
- Python package upload scripts now fail fast with a retirement message; Python
  package upload is no longer a supported release path for this native
  milestone.
