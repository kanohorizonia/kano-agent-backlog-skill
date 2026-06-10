# Setup Summary

`kano-agent-backlog-skill` now uses a native C++ executable contract.

## Developer Setup

```bash
cd kano-agent-backlog-skill
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
```

Use `scripts/kob` or `scripts/kano-backlog` to run the CLI. The launchers do
not fall back to Python.

## Runtime Contract

- Native binary: `src/cpp/out/bin/<preset>/<config>/kano-backlog`
- User wrapper: `scripts/kob`
- Python package install: retired
- Pytest oracle: removed
- Runtime `.py` / `.pyi` files: not allowed

## Release

Use native release gates and native artifacts. Python package publishing is
retired for this milestone.
