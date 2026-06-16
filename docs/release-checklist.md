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
- [ ] Verify Jenkins/cloud artifacts are root-level archives with checksum and manifest sidecars

### Native build notes

- [ ] Confirm CI/build environment has network access when using default FetchContent dependency modes.
- [ ] If deterministic/offline builds are required, document and use a pinned system/submodule/vcpkg strategy instead of implicit network fetches.

## Branch and Channel Model

- [ ] Keep source, version files, release notes, workflow definitions, and any package-manager templates on `main`
- [ ] Create GitHub Releases from annotated tags on `main`; do not use an orphan branch for release assets
- [ ] Publish generated GitHub Pages output to `gh-pages`; treat it as derived output
- [ ] Build Jenkins/GitHub Actions release artifacts from a pushed source revision or tag
- [ ] Use final release artifact URLs and SHA256 values before preparing package-manager manifests
- [ ] Confirm Jenkins `Release_Publish` generated package-manager review payloads under `Release/package-managers`

## Native Smoke Installation

- [ ] Start from a clean clone
- [ ] Run `pixi run build-dev`
- [ ] Run `bash scripts/kob doctor`
- [ ] Test basic workflow (init, create item, update state)

## Python Package Publishing

Python package publishing is retired for the `0.0.4` native release line. Do not upload to Test PyPI or PyPI from this repo.

## Publish GitHub Pages docs

- [ ] If docs changed, merge the docs changes to `main` or run `.github/workflows/pages.yml` with `workflow_dispatch`
- [ ] Confirm the `KanoAgentSkills / Publish Pages` workflow completes successfully
- [ ] Verify the updated site is live at `https://agentskill-backlog.kanohorizonia.com/`
- [ ] If using the branch-based fallback publish flow, confirm `CNAME` is restored in `gh-pages`

## Git and GitHub

- [ ] Commit all changes
- [ ] Create git tag: `git tag -a v0.0.4 -m "Release 0.0.4"`
- [ ] Push tag: `git push origin v0.0.4`
- [ ] Create GitHub release with release notes
- [ ] Attach native binary/archive artifacts to GitHub release

## Package Manager Preparation

- [ ] winget: prepare manifests after GitHub Release assets are final, then submit the required external PR to `microsoft/winget-pkgs`
- [ ] Homebrew: update the owned tap main branch after release assets are final; use a PR only if targeting `homebrew-core`
- [ ] apt: publish `.deb`, `pool/`, and `dists/` metadata to the configured apt package repository; do not use this repo's `gh-pages` branch unless static apt hosting is explicitly designed there
- [ ] Keep Jenkins package-manager publish flags disabled unless repository targets and approval policy are configured
- [ ] Confirm package-manager payloads reference the same artifact URLs and SHA256 values attached to the GitHub Release
- [ ] Record channel-specific evidence or external PR/repository links in the release worklog

## Post-Release

- [ ] Verify repo-local native binary works from a clean clone
- [ ] Update documentation if needed
- [ ] Announce release (if applicable)
- [ ] Monitor for issues
