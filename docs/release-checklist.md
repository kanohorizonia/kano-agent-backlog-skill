# Release Checklist for kano-agent-backlog-skill

This checklist ensures consistent release process and catches issues before publishing.

## Pre-Release

- [ ] Update `VERSION` and `src/python/kano_backlog_core/__version__.py`
- [ ] Update CHANGELOG.md with release notes
- [ ] Run full test suite: `pytest tests/`
- [ ] Run type checking: `mypy src/`
- [ ] Run linting: `black --check src/ && isort --check src/`
- [ ] Test CLI commands manually
- [ ] Verify `kano-backlog doctor` works
- [ ] Test multi-product workflow
- [ ] Review and update documentation

## Build

- [ ] Clean previous builds: `rm -rf dist/ build/ *.egg-info`
- [ ] Build package: `python -m build`
- [ ] Verify dist/ contains .tar.gz and .whl
- [ ] Inspect wheel contents: `python -m zipfile -l dist/*.whl`

## Test Installation

- [ ] Create fresh venv: `python -m venv test-venv`
- [ ] Install from wheel: `pip install dist/*.whl`
- [ ] Verify CLI available: `which kano-backlog` (or `where kano-backlog` on Windows)
- [ ] Run `bash scripts/internal/show-version.sh`
- [ ] Run `kano-backlog doctor`
- [ ] Test basic workflow (init, create item, update state)

## Upload to Test PyPI

- [ ] Upload with the maintainer script: `./scripts/publish_to_pypi.sh test` (or `.\scripts\publish_to_pypi.ps1 -Environment test` on Windows)
- [ ] Create fresh venv for testing
- [ ] Install from test.pypi: `pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Upload to PyPI

- [ ] Upload with the maintainer script: `./scripts/publish_to_pypi.sh prod` (or `.\scripts\publish_to_pypi.ps1 -Environment prod` on Windows)
- [ ] Verify package appears on pypi.org
- [ ] Create fresh venv
- [ ] Install from PyPI: `pip install kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Publish GitHub Pages docs

- [ ] If docs changed, merge the docs changes to `main` or run `.github/workflows/pages.yml` with `workflow_dispatch`
- [ ] Confirm the `Publish docs to GitHub Pages` workflow completes successfully
- [ ] Verify the updated site is live at `https://agentskill-backlog.kanohorizonia.com/`

## Git and GitHub

- [ ] Commit all changes
- [ ] Create git tag: `git tag -a v0.0.3 -m "Release 0.0.3"`
- [ ] Push tag: `git push origin v0.0.3`
- [ ] Create GitHub release with release notes
- [ ] Attach dist files to GitHub release

## Post-Release

- [ ] Verify pip install works: `pip install kano-agent-backlog-skill`
- [ ] Update documentation if needed
- [ ] Announce release (if applicable)
- [ ] Monitor for issues
