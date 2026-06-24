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
pixi run build-dev
```

Verify the CLI surface:

```bash
bash scripts/kob
bash scripts/kob doctor
```

Use the longer setup in [agent-quick-start.md](agent-quick-start.md) and [installation.md](installation.md). In a clone, the repo-local launcher is `bash scripts/kob` and it requires the native binary.

## 2. Initialize a backlog

From the repository where you want durable workflow memory:

```bash
bash scripts/kob admin init --product my-app --agent my-agent
```

This scaffolds local markdown artifacts under `_kano/backlog/`, including product config, backlog items, decisions, and generated views.

Use `--prefix` when the derived two-letter prefix would collide with another
product in the same shared backlog root. For example,
`kano-agent-ark-skill` should use `KOA`; `kano-ai-3d-asset-skill` should use a
distinct prefix such as `KA3D` or `K3DA`, not the ambiguous derived `KA`.

```bash
bash scripts/kob admin init --product kano-agent-ark-skill --prefix KOA --agent my-agent
bash scripts/kob admin init --product kano-ai-3d-asset-skill --prefix KA3D --agent my-agent
```

Add derived data to `.gitignore`:

```gitignore
.kano/cache
_kano/backlog/_shared/logs
```

## 3. Create a task before coding

```bash
bash scripts/kob item create --type task --title "Add authentication" --product my-app --agent my-agent \
  --duplicate-search-query "Add authentication" --duplicate-search-scope my-app --duplicate-decision create
```

This workflow is tickets first. Before implementation, search for existing work
and create the work item with duplicate-search admission evidence.

Use `--type issue` instead of `task` when the report is still pre-triage: an unclear problem, runtime gap, risk, or blocker that needs evidence before it can be split into a Task or Bug. Research, Decisions, and Spikes remain worklog, ADR, topic, tag, or artifact metadata rather than separate formal item types.

## 4. Satisfy the Ready Gate

Tasks, bugs, and issues are expected to carry enough context before active execution. Fill:

- Context
- Goal
- Approach
- Acceptance Criteria
- Risks / Dependencies

Example:

```bash
bash scripts/kob workitem set-ready MYAPP-TSK-0001 \
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
bash scripts/kob workset init --item MYAPP-TSK-0001 --agent my-agent
bash scripts/kob workset next --item MYAPP-TSK-0001
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
