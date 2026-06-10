# kano-agent-backlog-skill

[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-2ea44f.svg)](https://agentskill-backlog.kanohorizonia.com/)

Local-first backlog workflow for AI coding agents, with durable markdown work items, ADRs, worklogs, release evidence, and multi-agent handoff.

## Public thesis

AI coding sessions move fast, but the reasoning behind them disappears even faster. This project keeps the planning, decisions, verification targets, and handoff trail inside the repo as plain markdown so humans and agents can keep working from the same durable source.

## Why this exists

Most agent workflows still depend on fragile chat memory. That breaks down when you need to answer basic questions later, like why a task was split, what was accepted, which risk was known, or what the next agent should do.

`kano-agent-backlog-skill` exists to make agent work local first, reviewable, and recoverable. Instead of treating planning as disposable chat, it turns backlog items, ADRs, worklogs, and release evidence into repo assets.

## Native implementation direction

The repo-local executable contract is the native C++ CLI. The `scripts/kob` and `scripts/kano-backlog` launchers require a locally built native binary and no longer fall back to Python. The old Python runtime and pytest oracle have been removed from this repo; native C++ smoke tests and release gates are the supported verification surface.

## What it provides

- Markdown backed work items with frontmatter and stable IDs
- Ready gate fields for context, goal, approach, acceptance criteria, and risks
- Append only worklogs for execution history and handoff notes
- ADR support for durable technical decisions
- Worksets and topics for focused execution and multi-agent coordination
- Release evidence, views, and validation surfaces that stay in the repository
- Optional search and embedding flows, clearly marked as experimental

## Current release status

- `0.0.2` released
- this native milestone is not part of the old `0.0.3` Python-public release contract
- repo-local CLI usage is native C++ only
- Python package publishing is retired for this milestone
- Pre-1.0, so schema, CLI details, and public docs can still change

## Quick start

Build the native CLI from a cloned repository, then use the repo-local launcher:

```bash
pixi run build-dev
bash scripts/kob admin init --product my-app --agent codex
bash scripts/kob item create --type task --title "Add login" --product my-app --agent codex
bash scripts/kob doctor
```

If you are working from a clone, start with [docs/agent-quick-start.md](docs/agent-quick-start.md). The repo-local launcher is `bash scripts/kob`; it routes to the native binary only.

## Documentation

- [GitHub Pages documentation site](https://agentskill-backlog.kanohorizonia.com/)
- [Quick start](docs/quick-start.md)
- [Installation](docs/installation.md)
- [Configuration](docs/configuration.md)
- [Version policy](docs/version-policy.md)
- [Native CLI direction](docs/design/native-cli-direction.md)
- [Maintainer automation](docs/maintainer-automation.md)
- [Codex for OSS](docs/codex-for-oss.md)
- [Agent quick start](docs/agent-quick-start.md)
- [Usage examples](docs/usage-examples.md)
- [Worksets](docs/workset.md)
- [Topics](docs/topic.md)
- [Experimental features](docs/experimental-features.md)
- [Workflow reference](references/workflow.md)
- [Schema reference](references/schema.md)
- [REFERENCE.md](REFERENCE.md)
- [SKILL.md](SKILL.md)
- [CHANGELOG.md](CHANGELOG.md)

## Demo

A companion demo repo exists at [dorgonman/kano-agent-backlog-skill-demo](https://github.com/dorgonman/kano-agent-backlog-skill-demo).

It demonstrates the multi-agent adapter layout and a sample backlog workspace. This repo does not bundle that demo, so treat it as a separate example workspace. See [docs/demo-maintenance.md](docs/demo-maintenance.md) for the follow-up checklist used when the demo checkout is unavailable locally.

## GitHub Pages

Published docs live at [agentskill-backlog.kanohorizonia.com](https://agentskill-backlog.kanohorizonia.com/).

## Codex for OSS relevance

This project is aimed at open source maintainers and agent heavy teams who need more than generated code. It helps Codex style and other coding agents work against durable repo state instead of fading conversation state.

That matters for OSS review because maintainers need artifacts they can inspect in pull requests, not just claims from a previous chat. The backlog, ADR, worklog, and release evidence model is built around that review loop.

- issue triage and task refinement survive beyond chat history
- acceptance criteria and Ready Gate fields stay reviewable in markdown
- ADRs and worklogs preserve technical decisions for maintainers and future agents
- release notes, changelog inputs, and evidence snapshots stay attached to the repo
- multi-agent handoff is grounded in shared backlog artifacts instead of prompt reconstruction

## Validation

Validation here means keeping execution tied to explicit acceptance and visible evidence.

- Ready gate fields make tasks reviewable before coding starts
- `kano-backlog doctor` checks environment and backlog health
- Worklogs and release artifacts keep a visible trail of what changed and why
- References and generated views keep human review and agent handoff aligned

Recommended commands:

```bash
bash src/shell/test/quick-test.sh
bash src/shell/test/lint.sh
bash src/shell/support/show-version.sh
bash src/shell/support/self-build.sh debug
bash src/shell/test/native-test.sh
bash scripts/kob --version
bash scripts/kob doctor
```

## Experimental areas

Experimental work is present, but not sold as stable.

- Search and embedding flows
- Some advanced querying and tokenizer diagnostics
- Other surfaces called out in [docs/experimental-features.md](docs/experimental-features.md)

## License and contributing

Licensed under [MIT](LICENSE).

If you want to contribute, start with [CONTRIBUTING.md](CONTRIBUTING.md). For issues and feature requests, use the [GitHub issue tracker](https://github.com/kanohorizonia/kano-agent-backlog-skill/issues).
