# Installation

Repo-local execution uses the native C++ CLI. Build it from a clone.

## Quick build (developer checkout)

```bash
# Build release binary (default)
bash scripts/kob self build

# Explicit modes
bash scripts/kob self build release   # same as above
bash scripts/kob self build debug     # debug only when explicitly requested
```

`bash scripts/kob self build` is equivalent to `bash scripts/kob self build release`. Debug requires explicit `debug` argument.

### pixi build (also available)

```bash
pixi run build-dev
```

## Verify the installation

```bash
bash scripts/kob --version
bash scripts/kob doctor
bash scripts/kob self status    # shows binary path, config, mode
bash scripts/kob self doctor    # full health check
```

## Rebuild from scratch

```bash
bash scripts/kob self rebuild         # release (default)
bash scripts/kob self rebuild release # same as above
bash scripts/kob self rebuild debug   # debug only when explicitly requested
```

`self rebuild` clears the canonical preset's obj/bin directories and runs `self build`. Platform presets are aligned:

| Platform | Canonical preset |
| --- | --- |
| Windows | `windows-ninja-msvc` (fallback: `windows-msbuild`) |
| Linux | `linux-ninja-clang` |
| macOS | `macos-ninja-clang` |

## Notes

- `self build` and `self rebuild` only work in a **developer checkout** (requires `VERSION`, `src/cpp/CMakeLists.txt`, `pixi.toml`).
- Packaged installs must use the release/update flow, not `self build`.
- No Python runtime. No `~/.kano` build config. Build contract lives in `src/shell/support/self-build.sh`.
- Binary resolution is always release-first. Override with `KANO_BACKLOG_BINARY=/path/to/binary`.

If you are working from a clone of the skill repository, start with the repo level guidance in these pages:

- [Agent quick start](agent-quick-start.md)
- [Main repository README](https://github.com/kanohorizonia/kano-agent-backlog-skill#quick-start)

For repo-local execution, use `bash scripts/kob`. It requires a built native binary and does not fall back to Python.

For docs maintenance, the local docs pipeline lives under `src/shell/docs/` and builds into `_ws/build/staged` by default.

## Contributor setup

```bash
pixi run build-dev
pixi run quick-test
```

This is the supported path when working on the native executable. Python package metadata has been retired for this native milestone.

## Platform notes

- Windows users should prefer Git Bash or WSL for the bundled shell scripts.
- macOS and Linux can use the scripts directly.
- The repo also includes `pixi.toml` for native build and cross-platform helper tasks.

## Troubleshooting

- If `kob` cannot find a binary, run `bash scripts/kob self build` (or `pixi run build-dev`).
- Binary resolution is release-first. Set `KANO_BACKLOG_BINARY` to override.
- If docs dependencies are missing, use the native docs scripts under `src/shell/docs/`.
- If tests fail before launching the CLI, use `pixi run quick-test` to exercise the native smoke lane.
