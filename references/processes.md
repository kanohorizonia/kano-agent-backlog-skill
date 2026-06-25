# Process Profiles

Process profiles define work item types, states, and transitions for the local
backlog. They are intended to be human-readable and easy to adjust for agent
workflows.

## Suggested schema (TOML)

```toml
# Built-in Azure Boards Agile profile for kano-agent-backlog-skill.
id = "builtin/azure-boards-agile"
name = "Azure Boards Agile"
description = "Default Agile-like workflow for agent-managed backlog items."
default_state = "Proposed"
states = [
    "Proposed",
    "Planned",
    "Ready",
    "InProgress",
    "Review",
    "Blocked",
    "Done",
    "Duplicate",
    "Dropped"
]
terminal_states = ["Done", "Duplicate", "Dropped"]

[[work_item_types]]
type = "Initiative"
slug = "initiative"

[[work_item_types]]
type = "Epic"
slug = "epic"

[[work_item_types]]
type = "Feature"
slug = "feature"

[[work_item_types]]
type = "UserStory"
slug = "userstory"

[[work_item_types]]
type = "Task"
slug = "task"

[[work_item_types]]
type = "Bug"
slug = "bug"

[[work_item_types]]
type = "Issue"
slug = "issue"

[transitions]
Proposed = ["Planned", "Duplicate", "Dropped"]
Planned = ["Ready", "Duplicate", "Dropped"]
Ready = ["InProgress", "Duplicate", "Dropped"]
InProgress = ["Review", "Blocked", "Duplicate", "Dropped"]
Review = ["Done", "InProgress"]
Blocked = ["InProgress", "Duplicate", "Dropped"]
Done = []
Duplicate = []
Dropped = []
```

## Notes

- `work_item_types` should align with item frontmatter `type`.
- `states` should align with `state` values used in items.
- Keep transitions permissive for agent autonomy; tighten only if needed.

## State semantics (standard set)

These semantics apply to built-ins that use the default KABSD state set:

- Proposed: not ready to start; needs more discovery/confirmation.
- Planned: approved for the plan; detail refinement can proceed, but not started.
- Ready: Ready gate passed (typically for Task/Bug/Issue before start).
- InProgress: work started.
- Blocked: work started but blocked.
- Review: work complete pending review/verification.
- Done: work complete and accepted.
- Duplicate: terminal state for valid work whose canonical ownership belongs to
  another item. The item must carry `duplicate_of` pointing at the canonical
  item ID or UID; it is distinct from Dropped.
- Dropped: work intentionally stopped.

## Config selection

Use `process.profile` and `process.path` in your product config:

- `_kano/backlog/products/<product>/_config/config.toml`

to choose a built-in profile or a custom file.

Use `kano-backlog config show --product <product>` to confirm the effective merged config.

## Built-in profiles

- `references/processes/azure-boards-agile.toml` -> `builtin/azure-boards-agile`
- `references/processes/scrum.toml` -> `builtin/scrum`
- `references/processes/cmmi.toml` -> `builtin/cmmi`
- `references/processes/jira-default.toml` -> `builtin/jira-default`

## Custom profiles

Use the template as a starting point and store it under your product config
area (recommended: `_kano/backlog/products/<product>/_config/processes/`), then point
`process.path` to that file.

Template:
- `references/processes/template.toml`

Example config:

```toml
[process]
path = "_kano/backlog/products/<product>/_config/processes/custom.toml"
```
