# Codex for OSS

This guide describes how to use `kano-agent-backlog-skill` as an auditable memory layer when an OSS project relies on AI coding agents such as Codex style assistants.

## Positioning

The project is not presented here as a general purpose hosted task tracker. It is a local first repository workflow that keeps planning, rationale, and verification artifacts close to the codebase.

The current status is pre-1.0 and experimental. Use it as a disciplined workflow layer, not as a promise of stable long term interfaces.

## Why this project matters for OSS maintainers

Maintainers need a workflow memory layer that survives across sessions, handoffs, and pull requests. This project captures the planning and review artifacts that agent-heavy maintenance work usually leaves behind in chat history.

## Why it fits agent heavy OSS work

Open source maintenance often suffers when implementation moves faster than rationale. This repo addresses that by keeping durable artifacts in markdown:

- backlog items explain intended work
- Ready Gate fields define execution readiness
- worklogs capture decisions and state changes
- ADRs preserve architectural tradeoffs
- worksets reduce drift during active execution
- topics support handoffs and cross session context reuse

## Suggested operating model for Codex style agents

1. create or update the relevant work item before code changes
2. complete the Ready Gate for tasks and bugs
3. use a workset when implementation spans multiple steps
4. append worklog entries when the plan changes or a decision lands
5. create an ADR if the decision has lasting architectural impact
6. use a topic when several items, snippets, or documents need to travel together

The authoritative procedure is in [../references/workflow.md](../references/workflow.md).

## What is local first here

The canonical record lives in repository files, especially markdown work items, ADRs, worklogs, and related generated views. The project does not depend on a hosted control plane to preserve planning state.

## What is dogfooded

Based on the repo contents, the local backlog structure, release evidence flow, worksets, topics, and documentation pipeline are all actively used as part of maintaining this repository.

## Current limitations

- pre-1.0 interfaces can still change
- demo alignment is a separate follow-up when the demo checkout is unavailable locally
- optional search and embedding flows should be treated as experimental
- internal shared-infra APIs should not be described as stable public APIs

## 0.0.4 native release goals

- align public release metadata on `0.0.4`
- refresh README and maintainer-facing docs
- repair missing documentation links
- clean up the GitHub Pages landing page and deployment path
- describe the supported executable surface as native C++ only
- document demo follow-up instead of inventing unavailable demo changes

## What reviewers can inspect

Reviewers should be able to open the repository and inspect:

- item markdown
- linked ADRs
- generated views
- topic briefs
- promoted artifacts attached to the relevant item

That makes the project memory layer reviewable without replaying the original chat transcript.

## What not to claim yet

Keep public messaging conservative:

- don't imply stable APIs
- don't imply hosted collaboration features
- don't imply search is required for core use
- don't imply demo automation has already been refreshed unless that work was actually done

## Recommended OSS framing

For contributors and maintainers, describe this project as:

> a local first, markdown based workflow memory layer for agent assisted software work

That matches the current README thesis and the repository evidence.

## Related

- [quick-start.md](quick-start.md)
- [maintainer-automation.md](maintainer-automation.md)
- [../references/schema.md](../references/schema.md)
