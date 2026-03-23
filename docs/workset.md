# Workset (Execution Cache)

Workset is a **derived, ephemeral execution cache** used while an agent is working on a Task/Bug.
It is not the source of truth: canonical work items and ADRs remain the SSOT.

## Goals

- Prevent "agent drift" during longer tasks
- Provide an execution checklist and a place to capture notes/deliverables
- Make promotion back to canonical artifacts explicit (worklog, ADRs, attachments)

## Principles

- Workset data is derived and discardable (TTL cleanup is expected)
- Git must not track workset files
- Promote load-bearing information back to canonical items/ADRs

## Directory Layout

Per ADR-0011:

```text
_kano/backlog/.cache/worksets/items/<item-id>/
  meta.json       # Metadata: agent, timestamps, TTL, source paths
  plan.md         # Checklist derived from acceptance criteria
  notes.md        # Working notes (use Decision: markers for ADR promotion)
  deliverables/   # Files to promote to canonical artifacts
```

## CLI Commands

All workset commands are accessed via `kob workset <subcommand>`.

### Initialize a Workset

```bash
kob workset init --item <id> --agent <agent-name> [--ttl-hours 72] [--format plain|json]
```

Creates a workset for the specified item:
- Generates `meta.json` with agent, timestamps, and TTL
- Creates `plan.md` from item's acceptance criteria
- Creates `notes.md` with Decision: marker guidance
- Creates empty `deliverables/` directory
- Appends worklog entry to source item

If workset already exists, returns existing path (idempotent).

### Refresh from Canonical

```bash
kob workset refresh --item <id> --agent <agent-name> [--format plain|json]
```

Updates workset metadata from canonical files:
- Verifies source item still exists
- Updates `refreshed_at` timestamp in `meta.json`
- Appends worklog entry to source item

### Get Next Action

```bash
kob workset next --item <id> [--format plain|json]
```

Returns the next unchecked step from `plan.md`:
- Parses checkbox items (`- [ ]` and `- [x]`)
- Returns step number and description
- Returns completion message if all steps are checked

### Promote Deliverables

```bash
kob workset promote --item <id> --agent <agent-name> [--dry-run] [--format plain|json]
```

Promotes files from `deliverables/` to canonical artifacts:
- Copies files to `_kano/backlog/products/<product>/artifacts/<item-id>/`
- Appends worklog entry summarizing promoted files
- Use `--dry-run` to preview without making changes

### Cleanup Expired Worksets

```bash
kob workset cleanup [--ttl-hours 72] [--agent <agent-name>] [--dry-run] [--format plain|json]
```

Deletes worksets older than TTL:
- Only affects worksets under `.cache/worksets/items/`
- Reports count and space reclaimed
- Use `--dry-run` to preview without deleting

### List Worksets

```bash
kob workset list [--format plain|json]
```

Lists all item worksets with metadata:
- Item ID and agent
- Age and size
- TTL setting

### Detect ADR Candidates

```bash
kob workset detect-adr --item <id> [--format plain|json]
```

Scans `notes.md` for `Decision:` markers:
- Extracts decision text
- Suggests ADR title
- Use `--format json` for automation

## Common Workflows

### Starting Work on a Task

```bash
# 1. Initialize workset
kob workset init --item TASK-0042 --agent kiro

# 2. Check what to do first
kob workset next --item TASK-0042

# 3. Work on the task, updating plan.md as you go
# 4. Add notes with Decision: markers for important decisions
```

### Completing a Task

```bash
# 1. Check all steps are done
kob workset next --item TASK-0042
# Output: "✓ All steps complete!"

# 2. Promote any deliverables
kob workset promote --item TASK-0042 --agent kiro --dry-run
kob workset promote --item TASK-0042 --agent kiro

# 3. Check for ADR candidates
kob workset detect-adr --item TASK-0042
```

### Maintenance

```bash
# List all worksets
kob workset list

# Preview cleanup
kob workset cleanup --ttl-hours 48 --dry-run

# Run cleanup
kob workset cleanup --ttl-hours 48
```

## ADR Promotion

When `Decision:` markers are found in `notes.md`:

1. Run `kob workset detect-adr --item <id>` to find candidates
2. Create ADR using `kob adr create ...`
3. Link ADR to item via `decisions:` frontmatter
4. Worklog entry is appended automatically

## Git Ignore

Ensure cache paths are ignored:

```gitignore
_kano/**/.cache/
_kano/backlog/**/.cache/
```

## Related

- [Topic Context](topic.md) - Higher-level context grouping
- ADR-0011: Workset vs GraphRAG separation
- ADR-0012: Workset DB uses canonical schema
