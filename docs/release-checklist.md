# Release Checklist for kano-agent-backlog-skill

This checklist ensures consistent release process and catches issues before publishing.

## Pre-Release

- [ ] Update `VERSION`
- [ ] Update CHANGELOG.md with release notes
- [ ] Build native CLI: `pixi run build-dev`
- [ ] Run native tests: `pixi run quick-test`
- [ ] Run native coverage report: `bash src/shell/test/native-command-coverage.sh --strict`
- [ ] Run native runtime gate: `pixi run native-runtime-gate`
- [ ] Run native contract lint: `bash src/shell/test/lint.sh`
- [ ] Verify version surface: `bash scripts/internal/show-version.sh`
- [ ] Verify repo-local launcher version: `bash scripts/kob --version` (or `bash scripts/kob --help`)
- [ ] Verify repo-local launcher doctor: `bash scripts/kob doctor`
- [ ] Test multi-product workflow
- [ ] Review and update documentation

## Build

- [ ] Build release binary: `pixi run build-release`
- [ ] Run release gate workflow locally where possible
- [ ] Verify archive export: `bash scripts/kob export --single --validate-release-archive`

### Native build notes

- [ ] Confirm CI/build environment has network access when using default FetchContent dependency modes.
- [ ] If deterministic/offline builds are required, document and use a pinned system/submodule/vcpkg strategy instead of implicit network fetches.

## Native Smoke Installation

- [ ] Start from a clean clone
- [ ] Run `pixi run build-dev`
- [ ] Run `bash scripts/kob doctor`
- [ ] Test basic workflow (init, create item, update state)

## Python Package Publishing

Python package publishing is retired for this native milestone. Do not upload to Test PyPI or PyPI from this repo.

## Publish GitHub Pages docs

- [ ] If docs changed, merge the docs changes to `main` or run `.github/workflows/pages.yml` with `workflow_dispatch`
- [ ] Confirm the `KanoAgentSkills / Publish Pages` workflow completes successfully
- [ ] Verify the updated site is live at `https://agentskill-backlog.kanohorizonia.com/`
- [ ] If using the branch-based fallback publish flow, confirm `CNAME` is restored in `gh-pages`

## Git and GitHub

- [ ] Commit all changes
- [ ] Create git tag: `git tag -a v0.0.3 -m "Release 0.0.3"`
- [ ] Push tag: `git push origin v0.0.3`
- [ ] Create GitHub release with release notes
- [ ] Attach native binary/archive artifacts to GitHub release

## Post-Release

- [ ] Verify repo-local native binary works from a clean clone
- [ ] Update documentation if needed
- [ ] Announce release (if applicable)
- [ ] Monitor for issues
