# kano-agent-backlog-skill

[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-2ea44f.svg)](https://agentskill-backlog.kanohorizonia.com/)

**Evidence-first, repo-native Intent Engineering for AI coding agents.**

Turn messy human requests into durable Markdown work items with Ready gates,
worklogs, ADRs, and validation evidence so humans and agents can review the same
source of truth.

> Stop losing intent between chat requests, coding agents, CI logs, review
> comments, and the next handoff.

![Intent Engineering before and after](docs/assets/intent-engineering-before-after.svg)

## Why this exists

AI coding agents can produce convincing reports. That does not make the work
true.

KOB treats agent work as a falsifiable contract:

```text
human intent -> acceptance criteria -> bounded agent work -> validation artifact -> worklog evidence -> human review -> Done
```

The problem is not that agent teams lack task lists. The problem is that human
intent, acceptance criteria, risks, and validation evidence often evaporate
between conversations, tools, logs, and the next agent session. KOB keeps that
trail in the repository as reviewable Markdown.

## What this is

`kano-agent-backlog-skill` is a repo-native intent and evidence layer for AI
coding agents.

It provides:

- durable Markdown work items with stable IDs and frontmatter
- Ready gate fields for context, goal, approach, acceptance criteria, and risks
- Done gate discipline through validation evidence and human review
- append-only worklogs for execution history and handoff notes
- ADR and decision links for durable technical rationale
- worksets, topics, generated views, and release evidence that stay with the repo
- a native C++ CLI exposed through repo-local launchers such as `scripts/kob`

## What this is not

KOB is intentionally narrow. It is not:

- a general-purpose issue tracker
- a project-management suite
- a chat memory layer
- a model runtime
- an agent scheduler
- a CI replacement
- magic autonomous management

KOB does not prove correctness by asking an agent to repeat itself. It preserves
the contract and evidence chain that let humans review the work.

## Evidence-first Intent Engineering

Intent Engineering is the practice of turning ambiguous, partial, evolving human
intent into durable work items that an AI coding agent can execute and a
maintainer can review.

Core rule:

> A claim that cannot be falsified is not evidence.

An unfalsifiable claim may be a hypothesis, design intent, opinion, or future
note, but it should not close a work item. Done claims need bounded evidence:
acceptance criteria that could fail, commands or artifacts that can be inspected,
worklog entries that say what changed, and human review that can reject the
result.

![Evidence chain from human intent to Done](docs/assets/evidence-chain.svg)

## 60-second demo

From a cloned repository, build the native CLI and create one task:

```bash
pixi run build-dev
bash scripts/kob admin init --product my-app --agent local-agent
bash scripts/kob item create --type task --title "Add login" --product my-app --agent local-agent
```

Expected result:

```text
_kano/backlog/products/my-app/items/task/0000/<ID>_add-login.md
```

Open the generated Markdown file. You should see frontmatter plus human-readable
sections for Context, Goal, Approach, Acceptance Criteria, Risks, and Worklog.
Before implementation starts, fill the Ready fields so the agent has a
falsifiable contract instead of a vague prompt.

## Work item anatomy

![Durable work item anatomy](docs/assets/work-item-anatomy.svg)

A work item is not a disposable prompt. It is the durable artifact that carries
intent, scope, acceptance, risk, decisions, and evidence across agent sessions.

## Core workflow

1. Capture human intent in a Markdown work item.
2. Fill the Ready gate: context, goal, approach, acceptance criteria, and risks.
3. Start bounded agent work against that item.
4. Validate only the claims that need to be proven or falsified.
5. Record commands, reports, artifacts, and limitations in the Worklog.
6. Move to Review when evidence exists.
7. Close only after human review accepts the evidence chain.

![Agent handoff lifecycle](docs/assets/agent-handoff-lifecycle.svg)

![Ready and Done state lifecycle](docs/assets/state-lifecycle-ready-done.svg)

## Backboard

Backboard is the local read-only review surface for backlog state. The host path
builds the native KOB Webview runtime and serves Backboard on the workstation:

```bash
pixi run webview
```

The Docker path builds the image, starts a restartable container, and opens the
same local URL:

```bash
pixi run webview-docker
```

The CLI shortcut `kob gui` runs the same Docker path. Stop the container with
`pixi run webview-docker-down`.

The visual below is a concept mock for a future review-console style experience,
not a current UI screenshot.

![Conceptual human review console preview](docs/assets/webview-review-console-concept.svg)

## Current status

| Area | Current status |
| --- | --- |
| Runtime | Native C++ CLI through `scripts/kob` and `scripts/kano-backlog` after local build |
| Latest release | [GitHub Releases](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases) is the source of truth for public artifacts |
| Development marker | `0.0.5` is the current development marker for the Intent Engineering feature wave |
| Native release target | `0.0.4` is accepted only when `v0.0.4` has public downloadable artifacts and integrity metadata |
| Previous tagged OSS release | `0.0.2` |
| Backboard | Local read-only review surface exists through the KOB Webview runtime; review-console visual above is conceptual |
| Package manager channels | winget, Homebrew, and apt are planned/status-tracked channels, not live unless release metadata says so |
| Python runtime | Retired for this milestone; repo-local CLI usage is native C++ only |
| Stability | Pre-1.0; schema, CLI details, and docs can still change |

## Start here

| I want to... | Go to |
| --- | --- |
| Understand the concept | [Public docs homepage](https://agentskill-backlog.kanohorizonia.com/) |
| Try it locally | [Quick start](docs/quick-start.md) |
| Use it with agents | [Agent quick start](docs/agent-quick-start.md) |
| Learn commands | [CLI reference](https://agentskill-backlog.kanohorizonia.com/cli/commands) |
| Review architecture | [Design references](docs/design/native-cli-direction.md) and [references](references/) |
| Maintain releases | [Maintainer automation](docs/maintainer-automation.md) and [release channels](docs/release-channels.md) |

## Documentation

- [GitHub Pages documentation site](https://agentskill-backlog.kanohorizonia.com/)
- [Quick start](docs/quick-start.md)
- [Installation](docs/installation.md)
- [Configuration](docs/configuration.md)
- [Usage examples](docs/usage-examples.md)
- [Worksets](docs/workset.md)
- [Topics](docs/topic.md)
- [Version policy](docs/version-policy.md)
- [Release channels](docs/release-channels.md)
- [Native CLI direction](docs/design/native-cli-direction.md)
- [Backboard information architecture](docs/design/backboard-information-architecture.md)
- [Backboard Review Inbox model](docs/design/backboard-review-inbox-model.md)
- [Backboard partial UI boundary](docs/design/backboard-partial-ui-boundary.md)
- [Actor alias and assignment policy](docs/design/actor-alias-and-assignment-policy.md)
- [Product Map projection schema](docs/design/product-map-projection-schema.md)
- [ADR lifecycle metadata](docs/design/adr-lifecycle-metadata.md)
- [Feature evolution event model](docs/design/feature-evolution-event-model.md)
- [Design-history graph edge semantics](docs/design/design-history-graph-edge-semantics.md)
- [Version Goal Ledger schema](docs/design/version-goal-ledger-schema.md)
- [Evidence quality classification model](docs/design/evidence-quality-classification-model.md)
- [Context recovery summary contract](docs/design/context-recovery-summary-contract.md)
- [Maintainer automation](docs/maintainer-automation.md)
- [Agent quick start](docs/agent-quick-start.md)
- [Experimental features](docs/experimental-features.md)
- [Workflow reference](references/workflow.md)
- [Schema reference](references/schema.md)
- [REFERENCE.md](REFERENCE.md)
- [SKILL.md](SKILL.md)
- [CHANGELOG.md](CHANGELOG.md)

## Maintainer and release gates

Release evidence remains important, but it should not be the first thing a new
human sees.

Shared release acceptance gates for this skill line:

1. A version is not accepted as released until a non-draft GitHub Release exists
   for the version tag with downloadable, installable platform artifacts,
   integrity metadata, and an artifact index from the matching CI source/version.
2. The README and public documentation site must both link release downloads and
   describe manual installation plus package-channel status for each supported
   platform.

Internal CI archives, dry-run release runs, or staged payloads are review
evidence, but they do not close the public release gate without those public
surfaces.

## Install from a release

Download packages from the [latest GitHub Release](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/latest).
For the 0.0.4 line, the expected release tag is
[`v0.0.4`](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/tag/v0.0.4).

Manual install baseline:

```bash
# Linux or macOS: choose the archive matching your platform and CPU.
mkdir -p "$HOME/.agents/skills/kano-agent-backlog-skill"
tar -xzf KanoHorizonia.KanoBacklog-<platform>-main-<version>-Release-cli.tar.gz \
  -C "$HOME/.agents/skills/kano-agent-backlog-skill" --strip-components 1
export PATH="$HOME/.agents/skills/kano-agent-backlog-skill/scripts:$PATH"
kob --version
kob doctor
```

On Windows, use the Windows archive from the same release and add the extracted
`scripts` directory to `PATH`. When an MSI is published for a release, prefer the
MSI because it performs the skill install and PATH setup.

Package-manager channels:

- winget: planned package ID `KanoHorizonia.KanoBacklog`; use only after the
  release publishes winget metadata.
- Homebrew: planned formula `kano-backlog` in an owned Kano tap; `homebrew-core`
  is not used for the 0.0.4 validation path.
- apt: planned owned apt repository; no public apt repository is live until
  release metadata and repository indexes are published.

The documentation site carries the same download and package-manager status in
[Installation](https://agentskill-backlog.kanohorizonia.com/guides/installation)
and [Release Channels](https://agentskill-backlog.kanohorizonia.com/guides/release-channels).

## Native implementation direction

The repo-local executable contract is the native C++ CLI. The `scripts/kob` and
`scripts/kano-backlog` launchers require a locally built native binary and no
longer fall back to Python. The old Python runtime and pytest oracle have been
removed from this repo; native C++ smoke tests and release gates are the
supported verification surface.

Recommended validation commands:

```bash
bash src/shell/test/quick-test.sh
bash src/shell/test/lint.sh
bash src/shell/support/show-version.sh
bash src/shell/support/self-build.sh debug
bash src/shell/test/native-test.sh
bash scripts/kob --version
bash scripts/kob doctor
```

## Demo repository

A companion demo repo exists at
[dorgonman/kano-agent-backlog-skill-demo](https://github.com/dorgonman/kano-agent-backlog-skill-demo).
It demonstrates a sample backlog workspace. This repo does not bundle that demo,
so treat it as a separate example workspace. See
[docs/demo-maintenance.md](docs/demo-maintenance.md) for the follow-up checklist
used when the demo checkout is unavailable locally.

## Experimental areas

Experimental work is present, but not sold as stable:

- search and embedding flows
- advanced querying and tokenizer diagnostics
- other surfaces called out in [docs/experimental-features.md](docs/experimental-features.md)

## License and contributing

Licensed under [MIT](LICENSE).

If you want to contribute, start with [CONTRIBUTING.md](CONTRIBUTING.md). For
issues and feature requests, use the repository issue tracker.
