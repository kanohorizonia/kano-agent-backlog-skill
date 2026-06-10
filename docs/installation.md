# Installation

Repo-local execution uses the native C++ CLI. Build it from a clone:

```bash
pixi run build-dev
```

Verify the native launcher:

```bash
bash scripts/kob --version
bash scripts/kob doctor
```

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

- If `kob` cannot find a binary, run `pixi run build-dev`.
- If docs dependencies are missing, use the native docs scripts under `src/shell/docs/`.
- If tests fail before launching the CLI, use `pixi run quick-test` to exercise the native smoke lane.
