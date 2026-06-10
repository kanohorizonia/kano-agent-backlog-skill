<!-- kano-agent-backlog-skill:start -->
## Backlog workflow (kano-agent-backlog-skill)
- Skill entrypoint: `{{SKILL_ROOT}}/SKILL.md`
- Backlog root: `{{BACKLOG_ROOT}}`
- Before coding, create/update backlog items and meet the Ready gate.
- Worklog is append-only; record decisions and state changes.
- Prefer running the native `kob` CLI so actions are auditable (and dashboards stay current):
  - `{{SKILL_ROOT}}/scripts/kob admin init --product <name> --agent <agent-name>`
  - `{{SKILL_ROOT}}/scripts/kob workitem create|update-state ... --agent <agent-name> [--product <name>]`
  - `{{SKILL_ROOT}}/scripts/kob view refresh --agent <agent-name> --product <name>`
- Dashboards auto-refresh after item changes by default (`views.auto_refresh=true`); use `--no-refresh` or set it to `false` if needed.
- **Container note**: `admin init` requires the native binary for the container platform, not Python/pip.
<!-- kano-agent-backlog-skill:end -->
