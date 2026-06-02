# Kano Agent Backlog Skill

Local first backlog tooling for agent driven software work, with durable work items, worklogs, ADRs, and release evidence stored in the repo.

## Start here

- [Main repository](https://github.com/kanohorizonia/kano-agent-backlog-skill)
- [Demo repository](https://github.com/dorgonman/kano-agent-backlog-skill-demo)
- [Quick start](guides/quick-start.md)
- [Clone quick start](guides/agent-quick-start.md)
- [Installation](guides/installation.md)
- [Configuration](guides/configuration.md)
- [Version policy](guides/version-policy.md)
- [CLI reference](cli/commands.md)
- [Maintainer automation](automation/maintainer-automation.md)
- [Docs pipeline](automation/docs-pipeline.md)
- [Codex for OSS](guides/codex-for-oss.md)
- [Demo maintenance](guides/demo-maintenance.md)
- [Architecture decisions](adr/overview.md)
- [Release 0.0.3 notes](releases/0.0.3.md)

## What this site covers

This site brings together the published skill docs, generated CLI docs, the Quartz and MkDocs build pipeline, and selected demo workspace artifacts that explain how the backlog model is used in practice.

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

The docs build keeps the existing Quartz plus MkDocs hybrid. Local runs now stop at build and staging by default. The repository currently supports branch-based `gh-pages` publishing through `scripts/docs/`, and that flow restores the `CNAME` file from docs build config so the custom domain stays attached.

This project is currently preparing the `0.0.3` public OSS-readiness release. Experimental native and search-related areas remain available, but they are not the first thing a new reviewer should see.
