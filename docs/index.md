# Kano Agent Backlog Skill Technical Release

> Kano Agent Backlog Skill is local-first backlog tooling for agent-driven software work. It keeps durable work items, worklogs, ADRs, release evidence, and reviewable automation output in ordinary repository files.

> This file is the GitHub Pages home page source. If you are browsing the repository directly on GitHub, use [docs/README.md](README.md) for repo-local links.

## Publish identity

| Field | Value |
| --- | --- |
| Product | `kano-agent-backlog-skill` |
| CLI | `kob` / `kano-backlog` |
| Current release line | `0.0.4` native C++ |
| Source repository | [kanohorizonia/kano-agent-backlog-skill](https://github.com/kanohorizonia/kano-agent-backlog-skill) |
| CI workflow | [KanoAgentSkills / Cloud Build](https://github.com/kanohorizonia/kano-agent-backlog-skill/actions/workflows/agent-skill-cloud-build.yml) |

## Start here

| Destination | Why open it |
| --- | --- |
| [Quick start](guides/quick-start.md) | Install and run the backlog CLI in a local repo. |
| [Release downloads](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/latest) | Download installable platform artifacts from the latest GitHub Release. |
| [Installation](guides/installation.md) | Manual archive install and package-manager channel status. |
| [CLI reference](cli/commands.md) | Generated command surface for `kob`. |
| [Release 0.0.4 notes](releases/0.0.4.md) | Native C++ release scope and channel notes. |
| [Release channels](guides/release-channels.md) | How CI, GitHub Releases, Homebrew, winget, and apt publishing are intended to work. |
| [Architecture decisions](adr/overview.md) | Backlog model, ID policy, indexing, and local-first decisions. |
| [GitHub Actions](https://github.com/kanohorizonia/kano-agent-backlog-skill/actions) | Current cloud build, test, report, and Pages runs. |
| [Latest test report](reports/latest/test-report/) | Stable public test report HTML slot. |
| [Latest coverage report](reports/latest/coverage-report/) | Stable public coverage report HTML slot. |

## Release quality snapshot

| Signal | Current release policy |
| --- | --- |
| Required platform | Windows x64 |
| Optional cloud platforms | Windows arm64, Linux x64, Linux arm64, macOS x64, macOS arm64 |
| Test lane | Full native CTest lane with JUnit output |
| Public test report | Feature-first HTML report plus BDD scenario pages |
| Coverage report | Public source-level coverage is allowed for this open-source project |
| Package artifacts | Native CLI payloads must be downloadable from GitHub Releases before a version is accepted |

## Feature highlights

| Area | What the automation validates |
| --- | --- |
| Backlog core | Config loading, frontmatter parsing, state model behavior, and file layout contracts. |
| Workitem operations | Canonical item creation, IDs, templates, and repo-backed persistence. |
| CLI workflow | Repo-root discovery, noninteractive execution, topic/workset flows, and user-facing command behavior. |
| Release automation | Jenkins and GitHub Actions generate platform artifacts, test reports, coverage reports, and package-manager metadata. |

## What this site covers

This site brings together release-facing documentation, generated CLI docs, architecture decisions, release notes, and the CI report contract used by Jenkins and GitHub Actions. CI report pages are expected to expose both test detail and coverage detail because the project is open source.

## Core guides

- [Quick start](guides/quick-start.md)
- [Agent quick start](guides/agent-quick-start.md)
- [Worksets](guides/workset.md)
- [Topics](guides/topic.md)
- [Usage examples](guides/usage-examples.md)

## Tokenizer adapters

- [Tokenizer quick start](guides/tokenizer-quickstart.md)
- [Tokenizer adapters overview](guides/tokenizer-adapters.md)
- [Tokenizer configuration](guides/tokenizer-configuration.md)
- [Tokenizer CLI reference](guides/tokenizer-cli-reference.md)
- [Tokenizer troubleshooting](guides/tokenizer-troubleshooting.md)
- [Tokenizer performance](guides/tokenizer-performance.md)

## Maintainers

The docs build keeps the existing Quartz plus MkDocs hybrid. Local runs now stop at build and staging by default. The repository currently supports branch-based `gh-pages` publishing through `src/shell/docs/`, and that flow restores the `CNAME` file from docs build config so the custom domain stays attached.

This project is currently preparing the `0.0.4` native C++ release. The repo-local executable contract is native only; Python package entrypoints and PyPI publishing are retired for this release line.
