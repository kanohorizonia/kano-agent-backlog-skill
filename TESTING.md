# Testing Guide

`kano-agent-backlog-skill` is native C++ first. The supported test surface is the
native binary plus shell gates; the retired Python runtime and pytest oracle are
not part of this repo.

## Quick Start

```bash
cd skills/kano-agent-backlog-skill
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
bash src/shell/test/lint.sh
```

## Test Scripts

| Command | Purpose |
| --- | --- |
| `pixi run quick-test` | Builds/runs the native smoke tests. |
| `pixi run native-runtime-gate` | Verifies the native binary contract and absence of Python runtime/type-stub files. |
| `bash src/shell/test/native-command-coverage.sh --strict` | Native CLI command coverage inventory. |
| `bash src/shell/test/lint.sh` | Lightweight native-contract lint checks. |

## Native Test Targets

Native tests live under `src/cpp/code/tests/` and are built through the CMake
presets in `src/cpp/CMakePresets.json`. They cover core config/frontmatter,
workitem lifecycle, CLI repository workflows, tokenizer policy, and release
contract checks.

## Before Commit

```bash
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
bash src/shell/test/lint.sh
git diff --check
```

## Troubleshooting

### Native binary not found

Run:

```bash
pixi run build-dev
```

### Runtime gate finds Python files

Remove or port the reported files. This repo no longer allows checked-in
runtime `.py` or `.pyi` files.

### Windows assert dialogs during automation

The native CLI and smoke tests configure noninteractive Windows CRT/error
handling. If a dialog appears again, treat it as a test harness regression and
add the same noninteractive setup to the new executable/test entrypoint.
