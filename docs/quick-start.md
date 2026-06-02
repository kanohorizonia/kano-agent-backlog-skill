# Quick Start

`kano-agent-backlog-skill` is a pre-1.0, local-first backlog workflow for AI coding agents and maintainers. It stores planning, decisions, and review artifacts as markdown in your repository so work stays auditable and reproducible.

## What this guide covers

This guide gets you from install to a first usable backlog flow:

1. Install the CLI.
2. Initialize a product backlog.
3. Create a task.
4. Satisfy the Ready Gate.
5. Start execution with a workset.
6. Capture decisions in worklogs and ADRs.

For schema details, see [../references/schema.md](../references/schema.md). For the planning and execution SOP, see [../references/workflow.md](../references/workflow.md).

## 1. Install

```bash
pip install kano-agent-backlog-skill
```

Verify the CLI surface:

```bash
kano-backlog
kano-backlog doctor
```

If you are working from a cloned repository, use the longer setup in [agent-quick-start.md](agent-quick-start.md) and [installation.md](installation.md). In a clone, the repo-local launcher is `bash scripts/kob` (native first, Python fallback).

## 2. Initialize a backlog

From the repository where you want durable workflow memory:

```bash
kano-backlog admin init --product my-app --agent my-agent
```

This scaffolds local markdown artifacts under `_kano/backlog/`, including product config, backlog items, decisions, and generated views.

Add derived data to `.gitignore`:

```gitignore
.kano/cache
_kano/backlog/_shared/logs
```

## 3. Create a task before coding

```bash
kano-backlog item create --type task --title "Add authentication" --product my-app --agent my-agent
```

This workflow is tickets first. Before implementation, create the work item that explains the change.

## 4. Satisfy the Ready Gate

Tasks and bugs are expected to carry enough context before active execution. Fill:

- Context
- Goal
- Approach
- Acceptance Criteria
- Risks / Dependencies

Example:

```bash
kano-backlog workitem set-ready MYAPP-TSK-0001 \
  --product my-app \
  --context "Users need a reliable sign in flow" \
  --goal "Add authentication with clear verification steps" \
  --approach "Implement the feature, document it, and verify it end to end" \
  --acceptance-criteria "Login works and the change is documented" \
  --risks "Session handling and rollout may need follow-up"
```

## 5. Start focused execution with a workset

Worksets are per item execution caches that help reduce agent drift.

```bash
kano-backlog workset init --item MYAPP-TSK-0001 --agent my-agent
kano-backlog workset next --item MYAPP-TSK-0001
```

Use worksets when one task needs a staged checklist, working notes, or deliverables before promotion back to canonical backlog artifacts. See [workset.md](workset.md).

## 6. Capture decisions and handoffs

- Append worklog entries when direction changes or state changes.
- Create ADRs for meaningful architectural tradeoffs.
- Use topics when work spans multiple items or needs a reusable context bundle.

Topics are useful for cross file or cross session work because they gather items, pinned docs, snippets, and deterministic briefs. See [topic.md](topic.md).

## Optional experimental search

This repository documents optional indexing and search features, including SQLite and embedding based workflows. Treat them as experimental unless your own validation says otherwise.

Start with the deterministic backlog workflow first. Add search only if your team needs it.

## Next reading

- [installation.md](installation.md)
- [configuration.md](configuration.md)
- [maintainer-automation.md](maintainer-automation.md)
- [codex-for-oss.md](codex-for-oss.md)
- [../references/schema.md](../references/schema.md)
- [../references/workflow.md](../references/workflow.md)
