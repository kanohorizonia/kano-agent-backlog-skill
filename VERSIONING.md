# Versioning

This skill uses **Git tags** as the source of truth for released versions: `vX.Y.Z`.

## Where to check the current version

- File: `VERSION` (the intended version for the next release tag)
- Command: `cat VERSION`
- Release notes: `CHANGELOG.md`

## Pre-1.0 policy

While `< 1.0.0`, we treat releases as milestones and iterate quickly, but we still follow a predictable rule:

- `0.0.Z`: patch / bugfix / non-breaking improvement
- `0.Y.0`: may include breaking changes (schema/CLI/layout), with migration notes

## 1.0+ policy (SemVer)

After `1.0.0` we follow SemVer strictly:

- `X.Y.Z`
  - `Z` (patch): backward-compatible bugfix only
  - `Y` (minor): backward-compatible features + optional deprecations
  - `X` (major): breaking changes (must include migration guidance)

## What counts as breaking

Non-exhaustive examples:

- Renaming/removing required frontmatter keys, or changing the meaning of states/groups
- Changing the canonical backlog root layout (`_kano/backlog/**`) or bucket rules
- Removing/renaming CLI flags, or changing defaults that alter deterministic outputs
- Renaming/removing config keys under `_kano/backlog/products/<product>/_config/config.toml`
- Changing canonical dashboard filenames or their grouping semantics

## Release checklist (minimum)

- Docs reflect current behavior (`README*`, `REFERENCE.md`, `references/*`)
- Release notes exist in both locations:
  - `docs/releases/<version>.md`
  - `docs/releases/<version>.md` in the repo checkout used for release validation
- Release checks are run and reports are written:
  - Phase1 (version/docs checks):
    - `bash scripts/kob admin release check --version <version> --topic release-<version-dashed> --agent <id> --phase phase1 --product kano-agent-backlog-skill`
    - Expected report: `_kano/backlog/topics/release-<version-dashed>/publish/release_check_<version>_phase1.md`
  - Phase2 (doctor/native smoke):
    - `bash scripts/kob admin release check --version <version> --topic release-<version-dashed> --agent <id> --phase phase2 --product kano-agent-backlog-skill --sandbox release-<version-dashed>-smoke`
    - Expected report: `_kano/backlog/topics/release-<version-dashed>/publish/release_check_<version>_phase2.md`
    - Expected artifacts: `_kano/backlog/topics/release-<version-dashed>/publish/phase2_*.txt`
- Smoke topics must be sandboxed:
  - Smoke topics are disposable and must be created under `_kano/backlog_sandbox/<sandbox>/...`.
  - If you see `release-<version-dashed>-smoke-a/b` under `_kano/backlog/topics/`, delete them and re-run Phase2.
- Canonical CLI commands run end-to-end:
  - `bash scripts/kob view refresh --agent <id>`
  - `bash scripts/kob workitem update-state <item> --state Done --agent <id>`
- Demo views are regenerated
