# Release Checklist for kano-agent-backlog-skill

This checklist ensures consistent release process and catches issues before publishing.

## Pre-Release

- [ ] Update version in `src/kano_backlog_core/__version__.py`
- [ ] Update CHANGELOG.md with release notes
- [ ] Run full test suite: `pytest tests/`
- [ ] Run type checking: `mypy src/`
- [ ] Run linting: `black --check src/ && isort --check src/`
- [ ] Test CLI commands manually
- [ ] Verify `kob doctor` works
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
- [ ] Verify CLI available: `which kob` (or `where kob` on Windows)
- [ ] Run `bash scripts/internal/show-version.sh`
- [ ] Run `kob doctor`
- [ ] Test basic workflow (init, create item, update state)

## Upload to Test PyPI

- [ ] Upload: `twine upload --repository testpypi dist/*`
- [ ] Create fresh venv for testing
- [ ] Install from test.pypi: `pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Upload to PyPI

- [ ] Upload: `twine upload dist/*`
- [ ] Verify package appears on pypi.org
- [ ] Create fresh venv
- [ ] Install from PyPI: `pip install kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Git and GitHub

- [ ] Commit all changes
- [ ] Create git tag: `git tag -a v0.1.0 -m "Release 0.1.0 beta"`
- [ ] Push tag: `git push origin v0.1.0`
- [ ] Create GitHub release with release notes
- [ ] Attach dist files to GitHub release

## Post-Release

- [ ] Verify pip install works: `pip install kano-agent-backlog-skill`
- [ ] Update documentation if needed
- [ ] Announce release (if applicable)
- [ ] Monitor for issues
