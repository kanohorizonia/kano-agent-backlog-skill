# Agent Quick Start Guide

This guide is for AI agents helping users set up and use kano-agent-backlog-skill from a cloned repository.

## For Agents: When to Use This Guide

Use this guide when:
- User has cloned the skill repository
- User wants to use the native repo-local CLI
- User asks you to "initialize the backlog skill" or "set up kano-backlog"
- You need to help set up a local-first backlog system

## Native Repo-Local Setup

When working with a cloned repository, build the native CLI first. The repo-local launcher requires a native binary and does not fall back to Python.

### Step 0: Verify Prerequisites

Use the repository Pixi environment for repeatable native builds:

```bash
pixi run env-summary
```

### Step 1: Build the Native CLI

```bash
# Recommended: use self build (defaults to release)
bash scripts/kob self build

# Or via pixi
pixi run build-dev
```

`bash scripts/kob self build` is equivalent to `bash scripts/kob self build release`. Always builds release unless you pass `debug` explicitly.

To check status or rebuild from scratch:

```bash
bash scripts/kob self status    # shows binary path, mode, version
bash scripts/kob self rebuild   # clean + rebuild (release by default)
bash scripts/kob self doctor    # full health check
```

### Step 2: Run the Native Smoke Tests

```bash
pixi run quick-test
```

### Step 3: Verify Installation

```bash
# Check CLI is available
bash scripts/internal/show-version.sh
# Expected output: kob version from VERSION

# Inspect repo-local native launcher surface
bash scripts/kob --help

# Run environment check
bash scripts/kob doctor
```

## Initialization: Create First Backlog

After installation, initialize a backlog for the user's project.

### Step 1: Navigate to Project Root

```bash
# Go to the project root (where you want the backlog)
cd /path/to/user/project
```

### Step 2: Initialize Backlog

```bash
# Initialize with product name and agent identity
bash scripts/kob admin init --product <product-name> --agent <agent-id>

# Example:
bash scripts/kob admin init --product my-app --agent kiro
```

Use an explicit `--prefix` for Kano products whose derived prefix would collide
in a shared backlog root. `kano-agent-ark-skill` should be initialized with
`--prefix KOA`; `kano-ai-3d-asset-skill` should use a distinct prefix such as
`KA3D` or `K3DA`, not the ambiguous derived `KA`.

**What this creates:**
```
_kano/backlog/
├── products/
│   └── my-app/
│       ├── items/          # Work items organized by type
│       ├── decisions/      # Architecture Decision Records
│       ├── views/          # Generated dashboards
│       └── _meta/          # Metadata and sequences
```

### Step 3: Verify Structure

```bash
# Check that directories were created
ls -la _kano/backlog/products/my-app/

# Should see: items/, decisions/, views/, _meta/
```

### Step 4: Add Cache Directory to .gitignore (IMPORTANT)

**CRITICAL:** The backlog system creates cache files (SQLite databases, vector embeddings) that should NOT be committed to git.

**Add to your project's `.gitignore`:**

```bash
# Add cache and logs directories to .gitignore
echo "" >> .gitignore
echo "# Kano backlog cache and logs (derived data)" >> .gitignore
echo ".kano/cache" >> .gitignore
echo "_kano/backlog/_shared/logs" >> .gitignore
```

**Or manually edit `.gitignore` and add:**
```gitignore
# Kano backlog cache and logs (derived data)
.kano/cache
_kano/backlog/_shared/logs
```

**Why this is important:**
- Cache files can be large (embeddings, vector indexes)
- Log files accumulate over time and can grow large
- Both are derived data that can be regenerated
- Prevents merge conflicts on binary files
- Keeps repository size manageable

**What gets ignored:**
- `.kano/cache/backlog/` - Backlog-specific caches (chunks, embeddings)
- `.kano/cache/repo/` - Repository code analysis caches (if enabled)
- `_kano/backlog/_shared/logs/` - Audit logs and operation logs

## Common Agent Workflow

### Creating Work Items

**Before writing code, create a work item:**

```bash
# Create a task
bash scripts/kob item create \
  --type task \
  --title "Implement user authentication" \
  --product my-app \
  --agent kiro \
  --duplicate-search-query "Implement user authentication" \
  --duplicate-search-scope my-app \
  --duplicate-decision create

# Output: Created task: MYAPP-TSK-0001
```

`item create` and `workitem create` require duplicate-search admission evidence.
If similar candidates were found, include `--duplicate-candidate <ID>` and
`--duplicate-candidate-read <ID>` for each candidate you inspected. Creating
despite similar candidates also requires `--duplicate-override` and a non-empty
`--duplicate-rationale`.

**Fill in required fields before starting work:**

```bash
# Edit the item file
code _kano/backlog/products/my-app/items/task/0000/MYAPP-TSK-0001_*.md

# Add these sections:
# - Context: Why this work is needed
# - Goal: What success looks like
# - Approach: How you'll implement it
# - Acceptance Criteria: How to verify it works
# - Risks / Dependencies: What could go wrong
```

**Move to Ready state (enforces required fields):**

```bash
bash scripts/kob workitem set-ready MYAPP-TSK-0001 \
  --product my-app \
  --context "Users need secure authentication tokens" \
  --goal "Implement reliable authentication for the app" \
  --approach "Add auth handlers and verify the CLI workflow end to end" \
  --acceptance-criteria "Auth flow works and is documented" \
  --risks "Token storage and rollout need follow-up"
```

### State Transitions

```bash
# Start work
bash scripts/kob workitem update-state MYAPP-TSK-0001 \
  --state InProgress \
  --product my-app

# Complete work
bash scripts/kob workitem update-state MYAPP-TSK-0001 \
  --state Done \
  --product my-app
```

Use `Duplicate` only when this item is valid but canonical ownership belongs to
another item. The transition requires the canonical target:

```bash
bash scripts/kob workitem update-state MYAPP-TSK-0002 \
  --state Duplicate \
  --duplicate-of MYAPP-TSK-0001 \
  --message "Duplicate reconciliation: canonical_target=MYAPP-TSK-0001; outcome=duplicate" \
  --agent kiro \
  --product my-app
```

### Recording Decisions

**Create an ADR for significant decisions:**

```bash
bash scripts/kob adr create \
  --title "Use JWT for authentication" \
  --product my-app \
  --agent kiro

# Edit the ADR file to document:
# - Context: What's the situation?
# - Decision: What did you decide?
# - Consequences: What are the implications?
# - Alternatives: What else was considered?
```

## Agent Identity (CRITICAL)

**ALWAYS provide explicit `--agent` flag with your identity in EVERY command.**

This is a **required parameter** for auditability and worklog tracking. Commands will fail without it.

**Valid agent IDs:**
- `kiro` - Amazon Kiro
- `copilot` - GitHub Copilot
- `codex` - OpenAI Codex
- `claude` - Anthropic Claude
- `cursor` - Cursor AI
- `windsurf` - Windsurf
- `opencode` - OpenCode
- `antigravity` - Google Antigravity
- `amazon-q` - Amazon Q

**Never use placeholders like:**
- ❌ `<agent-id>`
- ❌ `<AGENT_NAME>`
- ❌ `auto`

**Example - ALL commands need --agent:**
```bash
# ✅ Correct
bash scripts/kob item create --type task --title "My task" --product my-app --agent kiro

# ❌ Wrong - missing --agent
bash scripts/kob item create --type task --title "My task" --product my-app
```

## Troubleshooting

### Windows: stale Python module errors

**Problem:** A stale Python-installed `kano-backlog.exe` from an older release is being used instead of the repo-local native launcher. Typical errors mention missing `kano_backlog_cli` or `kano_backlog_core` modules.

**Solution (Repo-local launcher):** Build the native binary and invoke the repo-local launcher from the clone:

```powershell
pixi run build-dev
bash scripts/kob item create --type task --title "My task" --product my-app --agent kiro
```

**Why this happens:**
- The old packaged Python wrapper and the repo-local native launcher are different execution paths
- The repo-local launcher now requires the current native build output

**Alternative:** Remove the stale Python console script from PATH and keep using `bash scripts/kob` from the repository root.

### "kob: command not found"

**Problem:** CLI not in PATH after installation.

**Solution:** Use the repo-local launcher:

```powershell
pixi run build-dev
bash scripts/internal/show-version.sh
bash scripts/kob --help
```

### "Missing --agent parameter"

**Problem:** Command fails with error about missing `--agent` parameter

**Solution:** The `--agent` flag is **REQUIRED** for all commands that modify the backlog:

```bash
# ❌ Wrong - will fail
bash scripts/kob item create --type task --title "My task" --product my-app

# ✅ Correct - includes --agent
bash scripts/kob item create --type task --title "My task" --product my-app --agent kiro
```

**Commands that require --agent:**
- `admin init`
- `adr create`
- `item create`
- `workitem set-ready`
- `worklog append`
- `workset init`
- `topic create`

See the [Agent Identity](#agent-identity-critical) section for valid agent IDs.

### "Invalid state transition"

**Problem:** Trying to skip required states (e.g., Proposed → Done)

**Solution:**
```bash
# Follow the state machine:
# Proposed → Planned → Ready → InProgress → Done

# Move through states sequentially:
bash scripts/kob workitem update-state <ID> --state Planned --product <product>
bash scripts/kob workitem set-ready <ID> --product <product> --context "..." --goal "..." --approach "..." --acceptance-criteria "..." --risks "..."
bash scripts/kob workitem update-state <ID> --state InProgress --product <product>
bash scripts/kob workitem update-state <ID> --state Done --product <product>
```

### "Ready gate validation failed"

**Problem:** Task/Bug/Issue missing required fields

**Solution:**
```bash
# Edit the item file and fill in all required sections:
# - Context
# - Goal
# - Approach
# - Acceptance Criteria
# - Risks / Dependencies

# Then try the state transition again
```

## Quick Reference

### Installation
```bash
cd skills/kano-agent-backlog-skill

# Build (release by default)
bash scripts/kob self build

# Or via pixi
pixi run build-dev

# Verify
bash scripts/kob self status
bash scripts/kob self doctor
bash scripts/kob --help
bash scripts/kob doctor
```

### Initialization
```bash
cd /path/to/project
bash scripts/kob admin init --product <product> --agent <agent>
```

### Common Commands
```bash
# Create item
bash scripts/kob item create --type task --title "<title>" --product <product> --agent <agent>

# List items
bash scripts/kob item list --product <product>

# Update state
bash scripts/kob workitem update-state <ID> --state <state> --product <product>

# Create ADR
bash scripts/kob adr create --title "<title>" --product <product> --agent <agent>

# Check environment
bash scripts/kob doctor
```

## Next Steps

After setup, guide the user through:

1. **Create their first work item** - Use `item create` to track work
2. **Understand the Ready gate** - Enforce required fields before starting work
3. **Learn state transitions** - Move items through the workflow
4. **Record decisions** - Use ADRs for significant technical choices
5. **Explore views** - Generate dashboards with `view refresh`

## Additional Resources

- **[Quick Start Guide](quick-start.md)** - Standard installation and usage
- **[Installation Guide](installation.md)** - Detailed setup and troubleshooting
- **[SKILL.md](../SKILL.md)** - Complete workflow rules for agents
- **[CONTRIBUTING.md](../CONTRIBUTING.md)** - Development guidelines
- **[Configuration Guide](configuration.md)** - Advanced configuration options

---

**Remember:** 
- **Always provide explicit `--agent` flags** for auditability
- **On Windows, if you encounter module or wrapper issues**, prefer the repo-local `kob` launcher after running the native build step
- Use `pixi run build-dev` and `pixi run quick-test` for native development. The quick lane is a bounded native smoke lane with explicit CTest timeout and stop-on-failure diagnostics; broader integration evidence belongs in `test`/`full-test` lanes through the shared infra manifest, plus `pixi run test-report` and coverage tasks.
