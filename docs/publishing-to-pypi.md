# Publishing to PyPI

Python package publishing is retired for the `0.0.4` native C++ release line.

The supported executable contract is the repo-local native binary launched through `scripts/kob` or `scripts/kano-backlog`. Release validation should use:

```bash
pixi run build-dev
pixi run quick-test
pixi run native-runtime-gate
bash src/shell/test/lint.sh
```

There is no repo-local PyPI publish script for this release line. Release artifacts must expose the native binary only.
