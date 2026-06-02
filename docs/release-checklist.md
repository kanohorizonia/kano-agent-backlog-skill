# Release Checklist for kano-agent-backlog-skill

This checklist ensures consistent release process and catches issues before publishing.

## Pre-Release

- [ ] Update `VERSION` and `src/python/kano_backlog_core/__version__.py`
- [ ] Update CHANGELOG.md with release notes
- [ ] Run full Python regression gate: `bash src/shell/test/quick-test.sh`
- [ ] Run lint/type gate: `bash src/shell/test/lint.sh`
- [ ] Verify version surface: `bash src/shell/support/show-version.sh`
- [ ] Build native CLI: `bash src/shell/support/self-build.sh debug`
- [ ] Run native tests: `bash src/shell/test/native-test.sh`
- [ ] Verify repo-local launcher version: `bash scripts/kob --version` (or `bash scripts/kob --help`)
- [ ] Verify repo-local launcher doctor: `bash scripts/kob doctor`
- [ ] Test multi-product workflow
- [ ] Review and update documentation

## Build

- [ ] Clean previous builds: `rm -rf dist/ dist-test/ build/ *.egg-info`
- [ ] Build package: `python -m build --sdist --wheel --outdir dist-test`
- [ ] Verify `dist-test/` contains .tar.gz and .whl
- [ ] Inspect wheel contents: `python -m zipfile -l dist-test/*.whl`

### Native build notes

- [ ] Confirm CI/build environment has network access when using default FetchContent dependency modes.
- [ ] If deterministic/offline builds are required, document and use a pinned system/submodule/vcpkg strategy instead of implicit network fetches.

## Test Installation

- [ ] Create fresh venv: `python -m venv test-venv`
- [ ] Install from wheel: `pip install dist/*.whl`
- [ ] Verify CLI available: `which kano-backlog` (or `where kano-backlog` on Windows)
- [ ] Run `bash scripts/internal/show-version.sh`
- [ ] Run `kano-backlog doctor`
- [ ] Test basic workflow (init, create item, update state)

## Upload to Test PyPI

- [ ] Upload with the maintainer script: `./src/shell/release/publish_to_pypi.sh test` (or `.\src\shell\release\publish_to_pypi.ps1 -Environment test` on Windows)
- [ ] Create fresh venv for testing
- [ ] Install from test.pypi: `pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Upload to PyPI

- [ ] Upload with the maintainer script: `./src/shell/release/publish_to_pypi.sh prod` (or `.\src\shell\release\publish_to_pypi.ps1 -Environment prod` on Windows)
- [ ] Verify package appears on pypi.org
- [ ] Create fresh venv
- [ ] Install from PyPI: `pip install kano-agent-backlog-skill`
- [ ] Verify installation and basic functionality

## Publish GitHub Pages docs

- [ ] If docs changed, merge the docs changes to `main` or run `.github/workflows/pages.yml` with `workflow_dispatch`
- [ ] Confirm the `Publish docs to GitHub Pages` workflow completes successfully
- [ ] Verify the updated site is live at `https://agentskill-backlog.kanohorizonia.com/`
- [ ] If using the branch-based fallback publish flow, confirm `CNAME` is restored in `gh-pages`

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
