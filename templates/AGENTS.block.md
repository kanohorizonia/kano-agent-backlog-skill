<!-- kano-agent-backlog-skill:start -->
## Project backlog discipline (kano-agent-backlog-skill)
- Use `{{SKILL_ROOT}}/SKILL.md` for any planning/backlog work.
- Backlog root is `{{BACKLOG_ROOT}}` (items are file-first; index/logs are derived).
- Before any code change, create/update items in `{{BACKLOG_ROOT}}/items/` (Epic -> Feature -> UserStory -> Task/Bug).
- Enforce the Ready gate on Task/Bug before starting; Worklog is append-only.
- Use the native `kob` CLI (not ad-hoc edits) so audit logs capture actions:
  - Bootstrap: `{{SKILL_ROOT}}/scripts/kob admin init --product <name> --agent <agent-name>`
  - Create/update: `{{SKILL_ROOT}}/scripts/kob workitem create|update-state ... --agent <agent-name>`
  - Views: `{{SKILL_ROOT}}/scripts/kob view refresh --agent <agent-name> --product <name>`
- Dashboards auto-refresh after item changes by default (`views.auto_refresh=true`); use `--no-refresh` or set it to `false` if needed.
- **Container note**: `admin init` requires the native binary for the container platform, not Python/pip.
<!-- kano-agent-backlog-skill:end -->
