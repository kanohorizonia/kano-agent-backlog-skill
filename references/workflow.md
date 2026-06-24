# Workflow SOP

## A) Planning (discussion -> tickets)

### Pre-planning: Sync DB sequences

Before creating any work items, ensure the DB sequence is synchronized:

```bash
# Sync sequences from filesystem to DB
kano-backlog admin sync-sequences --product <product>
```

**When to sync**:
- After cloning the repository
- After pulling changes that add/remove items
- When seeing "Ambiguous item reference" errors
- Before bulk item creation

### Planning workflow

1. Create or update Epic for the milestone.
2. Split into Features (capabilities).
3. Split into UserStories (user perspective).
4. Split into Tasks/Bugs (single focused coding sessions).
5. Use Issues only for pre-triage unclear problems, risks, blockers, or runtime gaps where the remediation type is not yet known.
6. Before creating an item, search for likely duplicates and pass the admission
   evidence to `item create` or `workitem create`.
7. Fill Ready gate sections for each Task/Bug/Issue before active work.
8. Append Worklog entry: "Created from discussion: ..." (scripts require `--agent`).

### Duplicate admission gate

Native `item create` and `workitem create` fail closed unless duplicate-search
admission evidence is present. The required evidence is:

- `--duplicate-search-query <query>`: the search text used before creation.
- `--duplicate-search-scope <scope>`: product, workset, or other searched scope.
- `--duplicate-decision <create|update|continue>`: why creation is allowed.

When similar candidates were found, pass each candidate with
`--duplicate-candidate <ID>` and each actually-read candidate with
`--duplicate-candidate-read <ID>`. Creating despite candidates also requires
`--duplicate-override` and non-empty `--duplicate-rationale <reason>`.

The command appends a Worklog summary and writes a durable receipt under
`_meta/duplicate-admission/<item-id>.json`.

## B) Ready gate

The Ready gate ensures planning is complete before execution begins.

**Required Fields** (Task/Bug/Issue):
- **Context**: Why are we doing this? What is the background?
- **Goal**: What is the specific objective?
- **Approach**: How will we solve it? (Steps, design choices)
- **Acceptance Criteria**: How do we know it's done? (Checklist)
- **Risks / Dependencies**: What could go wrong? What do we need first?

**Validation Points**:
1. **State Transition**: Moving to `InProgress` triggers automatic validation.
   - Checks item's required fields.
   - Checks parent's required fields (if parent is not null).
   - Blocks transition if validation fails.
   - Use `--force` to bypass (emergency only; records warning).
2. **Item Creation**: Creating a child item triggers parent validation.
   - Checks if parent is Ready.
   - Blocks creation if parent is not Ready.
   - Use `--force` to bypass.
3. **Manual Check**: `item check-ready <ID>` validates item and parent.

**Best Practices**:
- **Parent: null**: Allowed for standalone tasks, issues, top-level Epics, or hotfixes where hierarchy is overkill.
- **--force**: Use sparingly for hotfixes or when requirements are truly emergent.
- **Validation**: Always run `check-ready` before starting work.

Issues should not stay as a vague holding pen after triage. Once the problem is clear, split or link actionable remediation into Task/Bug items and record the relationship in `links.relates`, `links.blocks`, or the Worklog.

## C) Execution

1. Set state to InProgress.
2. Append Worklog for important decisions or changes.
3. If a decision is architectural, create ADR and link it:
   - Add ADR id to item `decisions: []`
   - Append Worklog entry referencing the ADR

### Conflict Guard

- **Owner Locking**: Items in `InProgress` are locked to their owner.
- **Auto-Assignment**: When moving to `InProgress`, if no owner is set, you become the owner.
- **Collaboration**: To hand off work, the current owner must change the owner field or move the item out of `InProgress` (e.g. to `Review` or `Planned`).

## D) Completion

1. Move state to Review -> Done.
2. Append a Worklog summary with:
   - What changed
   - Related items and ADRs
   - Branch convergence for code changes: `Branch convergence: target=<branch>`, `implementation_commit=<sha>`, `reachable_from_target=true`, and `remote_publication=<remote/ref>`.
3. The target is the repo default branch unless a human explicitly names another target. Side-branch-only Done requires `side_branch_delivery=explicit-human-choice` or `side_branch_delivery=human-approved`.
4. Nested/submodule work needs `nested_gitlink=<parent gitlink/submodule pointer evidence>`.
5. If convergence is blocked, keep the item out of Done and record `Blocked convergence: branch=<branch>; reason=<reason>; next=<step>; blocker=<owner/item>`.

## D.1) Parent sync (forward-only)

- When a child state changes, parents can be auto-advanced forward-only.
- Parent edits never force child states.
- Use `--no-sync-parent` if you need to keep parent state unchanged for a manual re-plan.

## E) Scope change

- Do not rewrite a ticket into a different task.
- Split into a new ticket and link via `links.relates`.
- Append a Worklog entry explaining the split.

## E.1) Duplicate reconciliation

Use `Duplicate` for a valid source item whose canonical target is another item:

```bash
kano-backlog workitem update-state KABSD-TSK-0002 \
  --state Duplicate \
  --duplicate-of KABSD-TSK-0001 \
  --message "Duplicate reconciliation: canonical_target=KABSD-TSK-0001; outcome=duplicate" \
  --agent <agent> \
  --product <product>
```

Do not use `Duplicate` for work that was abandoned or no longer desired; use
`Dropped` for that case. The Duplicate transition requires `--duplicate-of` and
rejects self-references.

## F) File operations

- Use `scripts/backlog/*` or `scripts/fs/*` for backlog/skill artifacts.
- Scripts only operate under `_kano/backlog/` or `_kano/backlog_sandbox/` to keep audit logs clean.

## F.1) ID management best practices

### Always use system-allocated IDs

**Correct**:
```bash
# Let the system allocate the next ID
kano-backlog item create --type task --title "..." --agent <agent> --product <product> \
  --duplicate-search-query "..." --duplicate-search-scope <product> --duplicate-decision create
# Output: OK: Created: KABSD-TSK-0340
```

**Incorrect**:
```yaml
# DON'T manually edit frontmatter to assign IDs
id: KABSD-TSK-9999  # ❌ Will cause collisions
```

### Reference items by UID when ambiguous

**Correct**:
```bash
# Use UID to avoid ambiguity
kano-backlog item update-state 019c11e6-de87-7218-b89b-38c2e4e9cabd \
  --state Done --agent <agent> --product <product>
```

**Incorrect**:
```bash
# Display ID may be ambiguous if duplicates exist
kano-backlog item update-state KABSD-TSK-0001 --state Done ...
# Error: Ambiguous item reference 'KABSD-TSK-0001': 2 matches
```

### Use trash command, never delete directly

**Correct**:
```bash
# Move to _trash/ (recoverable)
kano-backlog admin items trash <UID> \
  --agent <agent> \
  --reason "Duplicate/incorrect item" \
  --product <product> \
  --apply
```

**Incorrect**:
```bash
# DON'T delete files directly
rm _kano/backlog/products/<product>/items/task/0000/KABSD-TSK-0001_*.md  # ❌
```

### Periodic health checks

Run these commands periodically:

```bash
# Check DB sequence health
kano-backlog doctor --product <product>

# Validate UID uniqueness
kano-backlog admin validate uids --product <product>

# Sync sequences if needed
kano-backlog admin sync-sequences --product <product>
```

## G) Artifacts directory

Store work outputs in `_kano/backlog/products/<product>/artifacts/<item-id>/`:

**What to store**:
- Demo reports (e.g., `DEMO_REPORT_*.md`)
- Implementation summaries (e.g., `*_IMPLEMENTATION.md`)
- Analysis documents and investigation results
- Test results and benchmark data
- Generated diagrams and visualizations
- Exported data or intermediate build artifacts

**Why**:
- **Traceability**: Artifacts are directly linked to the work item that produced them
- **Context**: All outputs related to a specific Epic/Feature/Task are co-located
- **Archival**: When an item is completed, all related artifacts are preserved together
- **Discovery**: Easy to find outputs by navigating to the item's artifacts directory

**Example**:
```
artifacts/
├── KABSD-EPIC-0003/
│   ├── DEMO_REPORT_0.0.2_FINAL_SUCCESS.md
│   ├── UID_VALIDATION_IMPLEMENTATION.md
│   └── benchmark_results.json
└── KABSD-TSK-0123/
    ├── analysis.md
    └── diagram.png
```

**Note**: Do not store artifacts in repo root or scattered locations; always use the structured artifacts directory.
