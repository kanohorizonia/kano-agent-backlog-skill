# Installation

Install the package from PyPI when you want the published CLI:

```bash
pip install kano-agent-backlog-skill
```

Verify the install:

```bash
kano-backlog
kano-backlog doctor
```

If you are working from a clone of the skill repository, start with the repo level guidance in these pages:

- [Agent quick start](agent-quick-start.md)
- [Main repository README](https://github.com/kanohorizonia/kano-agent-backlog-skill#quick-start)

For docs maintenance, the local docs pipeline lives under `scripts/docs/` and builds into `_ws/build/staged` by default.

## Editable install for contributors

```bash
python -m pip install -e ".[dev]"
```

This is the recommended path when working on the skill itself, because it keeps the CLI and Python package tied to your local checkout.

## Platform notes

- Windows users should prefer Git Bash or WSL for the bundled shell scripts.
- macOS and Linux can use the scripts directly.
- The repo also includes `pixi.toml` for native build and cross-platform helper tasks.

## Troubleshooting

- If `kob` is not found, confirm the environment where the package was installed is active.
- If docs dependencies are missing, install `mkdocs`, `mkdocs-material`, `mkdocstrings[python]`, and `pyyaml` before running the docs pipeline.
- If tests cannot import the package from source, use the provided `scripts/test/*.sh` wrappers, which set `PYTHONPATH` correctly.
