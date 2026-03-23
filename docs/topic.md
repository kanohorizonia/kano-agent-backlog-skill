# Topic (Context Grouping)

Topics provide a **higher-level grouping mechanism** for rapid context switching when users change focus areas during a conversation.
While worksets are per-item execution caches, topics group multiple items and documents into a coherent context bundle.

## Goals

- Enable rapid context switching between focus areas
- Group related items and documents for coherent context loading
- Support multi-item work sessions without losing context
- Provide deterministic context export for agent consumption

## Principles

- Topics are derived data, but `brief.generated.md` is deterministic and can be shared/reviewed in-repo
- `brief.md` is stable and human-maintained (do not overwrite automatically)
- One active topic per agent at a time (tracked in shared cache state)
- Topics reference items by UID/ID (not copies)
- Pinned documents provide additional context beyond items
- `manifest.json` is primarily **machine-oriented** (for agents/tools); capture **human decision material** in `notes.md` (and/or pinned docs) so `brief.md` can remain deterministic

## When to Create a Topic (Rubric)

Topics are a shared, mid-term context buffer. Create a Topic when the context is likely
to be reused, revisited, handed off, or split into multiple work items.

Hard triggers (agent MAY create immediately):
- 2+ backlog work items are expected (or likely to be created) to complete the effort.
- Cross-module / multi-file work requires tracking multiple code locations or snippet refs.
- Work is expected to span multiple sessions or be handed off across agents.
- You are collecting durable evidence/materials (logs, snippet refs, pinned docs) that
  should be preserved.

Soft triggers (ask the human once before creating):
- You have entered an explore -> adjust -> re-explore loop 2+ times (context is no longer
  linear).
- The thread references 3+ distinct information sources that should stay linked (files,
  ADRs/docs, external links).
- There are 2+ unresolved decisions (A vs B) that will change the downstream plan.
- The user keeps appending new constraints/scope in the same thread (for example: 3+
  follow-ups).

Anti-triggers (prefer Workset or no Topic):
- Single-item execution where a clear Task/Bug exists and you are ready to implement
  (use a Workset).
- Small, single-file change with low risk of handoff or revisiting.
- Pure Q&A / explanation with no need to preserve artifacts or evidence.

After creating a Topic, always tell the human where to find it:
- Topic: <topic-name>
- Path: _kano/backlog/topics/<topic-name>/
- Human brief: _kano/backlog/topics/<topic-name>/brief.md (and brief.generated.md)
- List: kob topic list --agent <agent-id>

## Directory Layout

Topics live in the backlog tree so the deterministic brief can be shared:

```text
_kano/backlog/topics/<topic-name>/
  manifest.json     # Topic metadata: seed_items, pinned_docs, snippet_refs, timestamps, status
  brief.md          # Stable, human-maintained brief (do not overwrite automatically)
  brief.generated.md# Deterministic distilled brief (generated/overwritten by `kob topic distill`)
  notes.md          # Human-oriented notes/decision pack (freeform; intended for collaboration)
  materials/        # Raw collected materials (treated as cache; typically gitignored)
    clips/
    links/
    extracts/
    logs/
  synthesis/        # Derived working outputs (optional)
  publish/          # Prepared write-backs / patch skeletons (optional)

_kano/backlog/.cache/worksets/
  state.json                 # Shared topic state across agents
  topics/<slug>_<uuid>.json  # Topic state entries (cache)
```

Notes:
- This demo repo ignores `_kano/backlog/topics/**/materials/` by default.
- The active-topic pointer is per-agent and is not intended to be versioned.

### Manifest Structure

```json
{
  "topic": "auth-refactor",
  "agent": "kiro",
  "seed_items": ["TASK-0042", "TASK-0043", "BUG-0012"],
  "pinned_docs": ["_kano/backlog/decisions/ADR-0015.md"],
  "snippet_refs": [],
  "status": "open",
  "closed_at": null,
  "created_at": "2026-01-12T10:00:00Z",
  "updated_at": "2026-01-12T14:30:00Z"
}
```

## Human Decision Materials (Recommended)

Use `notes.md` as a lightweight “decision pack” that is easy for humans to review, while keeping `manifest.json` reference-first and `brief.md` deterministic.

Suggested `notes.md` sections:
- **Decision to make**: the specific question(s) that need a call
- **Options**: candidate approaches, with pros/cons
- **Evidence**: links to pinned ADRs, key snippets, benchmarks/logs
- **Recommendation**: proposed choice + rationale + follow-ups

## CLI Commands

All topic commands are accessed via `kob topic <subcommand>`.

### Create a Topic

```bash
kob topic create <topic-name> --agent <agent-name> \
  [--template <name> | --list-templates] [--var key=value ...] \
  [--no-notes] [--with-spec] [--format plain|json]
```

Creates a new topic:
- Validates topic name (alphanumeric, hyphens, underscores)
- Creates `manifest.json` with empty seed_items/pinned_docs/snippet_refs
- Creates `brief.md` template and topic subfolders
- Creates `spec/` structure if `--with-spec` is used (requirements.md, design.md, tasks.md)

### Create a Topic with Spec

```bash
kob topic create complex-feature --agent kiro --with-spec
```
This generates the **Spec Triad** (Requirements, Design, Tasks) in a specific `spec/` subdirectory, enabling rigorous feature definition (Medium-Term Memory).

### Topic Templates

List available templates:

```bash
kob topic template list
```

Create from a template:

```bash
kob topic create <topic-name> --agent <agent-name> --template <template-name> \
  --var key=value --var other=value
```

### Add Items to Topic

```bash
kob topic add <topic-name> --item <id> [--format plain|json]
```

Adds a backlog item to the topic:
- Verifies item exists in backlog
- Adds item UID to `seed_items` array
- Skips if item already in topic (idempotent)
- Updates `updated_at` timestamp

### Pin Documents

```bash
kob topic pin <topic-name> --doc <path> [--format plain|json]
```

Pins a document to the topic:
- Verifies document exists
- Adds path to `pinned_docs` array
- Skips if already pinned (idempotent)
- Supports relative paths from workspace root

### Switch Active Topic

```bash
kob topic switch <topic-name> --agent <agent-name> [--format plain|json]
```

Switches the active topic for an agent:
- Updates `active_topic.<agent>.txt`
- Returns summary (item count, pinned doc count)
- Shows previous topic if any

### Collect a Code Snippet

```bash
kob topic add-snippet <topic-name> --file <path> --start <line> --end <line> [--agent <agent>] [--snapshot]
```

Collects a reference-first code snippet into the topic manifest:
- Stores file path + line range + content hash
- Optional `--snapshot` caches the text in the manifest (still treated as derived)

### Distill Deterministic Brief

```bash
kob topic distill <topic-name>
```

Generates/overwrites `brief.generated.md` deterministically from the manifest + materials index.

`brief.md` is a stable, human-maintained brief. Use it for curated summaries that should not be overwritten by automation.

### Decision Audit and Write-back

Topics are a good place to capture and distill decisions, but the durable record must also live in the relevant work items.
This workflow helps humans verify that topic decisions were written back.

1) Generate a decision audit report for a topic:

```bash
kob topic decision-audit <topic-name>
kob topic decision-audit <topic-name> --format json
```

This writes a deterministic report to:

```text
_kano/backlog/topics/<topic-name>/publish/decision-audit.md
```

2) Write back a decision to a work item:

```bash
kob workitem add-decision <ITEM_ID_OR_PATH> \
  --decision "<English decision text>" \
  --source "_kano/backlog/topics/<topic-name>/synthesis/<file>.md" \
  --agent <agent-id> \
  --product <product>
```

This appends the decision under a `## Decisions` section in the item body (creating it if needed) and adds a Worklog entry.

### Close and Cleanup

```bash
kob topic close <topic-name> --agent <agent-name>
kob topic cleanup --ttl-days 14
kob topic cleanup --ttl-days 14 --apply
```

Closing marks the topic as closed; cleanup removes raw materials after TTL (and may optionally delete closed topics depending on implementation flags).

### Snapshots (Create/List/Restore/Cleanup)

Snapshots are lightweight, local-first checkpoints stored under:

```text
_kano/backlog/topics/<topic-name>/snapshots/
```

Commands:

```bash
kob topic snapshot create <topic-name> <snapshot-name> --agent <agent-id> [--description "..."] [--no-materials]
kob topic snapshot list <topic-name>
kob topic snapshot restore <topic-name> <snapshot-name> --agent <agent-id>
kob topic snapshot cleanup <topic-name> --ttl-days 14 --keep-latest 5
kob topic snapshot cleanup <topic-name> --ttl-days 14 --keep-latest 5 --apply
```

### Cross-References (related_topics)

Topics can be linked bidirectionally to support navigation and later graph-assisted retrieval.

```bash
kob topic add-reference <topic-a> --to <topic-b>
kob topic remove-reference <topic-a> --to <topic-b>
```

### Merge and Split

Merge multiple topics into a target topic:

```bash
kob topic merge <target-topic> <source-topic-1> <source-topic-2> --agent <agent-id> --dry-run
kob topic merge <target-topic> <source-topic-1> <source-topic-2> --agent <agent-id>
```

Split a topic into multiple subtopics:

```bash
kob topic split <source-topic> --agent <agent-id> \
  --new-topic "new-topic-a:ITEM-1,ITEM-2" \
  --new-topic "new-topic-b:ITEM-3"
```

### Shared Topic State (Active Topic Tracking)

Active topic pointers are stored in shared cache state:

- `_kano/backlog/.cache/worksets/state.json`
- `_kano/backlog/.cache/worksets/topics/*.json`

Inspect shared state:

```bash
kob topic show-state
kob topic list-active
```

Migrate legacy pointers (if you still have `active_topic.<agent>.txt` files):

```bash
kob topic migrate
kob topic cleanup-legacy --no-dry-run
```

### Export Context Bundle

```bash
kob topic export-context <topic-name> [--format markdown|json]
```

Exports topic context as a bundle:
- Loads summaries of all seed items (title, state, type)
- Includes content from pinned documents
- Output is deterministic (sorted, consistent formatting)
- Use `--format json` for machine parsing

### List Topics

```bash
kob topic list [--agent <agent-name>] [--format plain|json]
```

Lists all topics:
- Shows item count and pinned doc count
- Marks active topic (if `--agent` specified)
- Shows last updated timestamp

## Common Workflows

## Agent-first (Conversational) Workflows

This repo is conversational-first: docs should include both CLI usage and copy/paste prompts that a human can say to an AI agent.

Use this consistent format:
1) **Say this to your agent**
2) **The agent will do**
3) **Expected output**

### Create a Topic and Gather Context

Say this to your agent:
"Create a topic named <topic-name> for <problem>, add items <ITEM_1>, <ITEM_2>, pin <DOC_PATH>, then distill a generated brief." 

The agent will do:
- `kob topic create <topic-name> --agent <agent-id>`
- `kob topic add <topic-name> --item <ITEM_1>` (repeat for other items)
- `kob topic pin <topic-name> --doc <DOC_PATH>` (optional)
- `kob topic distill <topic-name>`

Expected output:
- `_kano/backlog/topics/<topic-name>/manifest.json` updated with seed items/pinned docs
- `_kano/backlog/topics/<topic-name>/brief.generated.md` regenerated deterministically

### Audit Decision Write-back for a Topic

Say this to your agent:
"Run a decision write-back audit for topic <topic-name> and show which work items are missing decisions. Save the report under publish/." 

The agent will do:
- `kob topic decision-audit <topic-name> --format plain`

Expected output:
- `_kano/backlog/topics/<topic-name>/publish/decision-audit.md`

### Write Back a Decision to a Work Item

Say this to your agent:
"Write back this decision to <ITEM_ID> and cite the synthesis file as the source: '<DECISION_TEXT>'." 

The agent will do:
- `kano workitem add-decision <ITEM_ID> \
  --decision "<DECISION_TEXT>" \
  --source "_kano/backlog/topics/<topic-name>/synthesis/<file>.md" \
  --agent <agent-id> \
  --product <product>`

Expected output:
- The work item markdown is updated with a `## Decisions` section (created if missing)
- A new Worklog entry is appended documenting the decision write-back

### Setting Up a Focus Area

```bash
# 1. Create topic for your work area
kob topic create auth-refactor --agent kiro

# 2. Add related items
kob topic add auth-refactor --item TASK-0042
kob topic add auth-refactor --item TASK-0043
kob topic add auth-refactor --item BUG-0012

# 3. Pin relevant documents
kob topic pin auth-refactor --doc _kano/backlog/decisions/ADR-0015.md
kob topic pin auth-refactor --doc docs/auth-design.md

# 4. Switch to the topic
kob topic switch auth-refactor --agent kiro
```

### Context Switching

```bash
# Check current topics
kob topic list --agent kiro

# Switch to different focus area
kob topic switch payment-flow --agent kiro

# Export context for agent consumption
kob topic export-context payment-flow --format json
```

### Loading Context into Agent

```bash
# Export as markdown for human review
kob topic export-context auth-refactor

# Export as JSON for programmatic use
kob topic export-context auth-refactor --format json
```

## Integration with Worksets

Topics and worksets work together:

1. **Topic**: Groups related items/docs/snippets and provides a deterministic brief
2. **Workset**: Per-item execution cache (plan/notes/deliverables) while implementing a specific Task/Bug

Typical flow:
```bash
# Switch to topic
kob topic switch auth-refactor --agent kiro

# Initialize workset for specific item
kob workset init --item TASK-0042 --agent kiro

# Work on item with workset
kob workset next --item TASK-0042

# When done, switch topic or continue with next item
```

## Git Ignore

Worksets are stored in `.cache/` and should be ignored.

Topic raw materials are treated as cache; in this demo repo we ignore:

```gitignore
_kano/backlog/**/.cache/
_kano/backlog/topics/**/materials/
```

## Related

- [Workset](workset.md) - Per-item execution cache
- Workset docs: `docs/workset.md`
