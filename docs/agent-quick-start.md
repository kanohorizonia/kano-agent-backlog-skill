# Agent Quick Start Guide

This guide is for AI agents helping users set up and use kano-agent-backlog-skill from a cloned repository.

## For Agents: When to Use This Guide

Use this guide when:
- User has cloned the skill repository (not installed from PyPI)
- User wants to use the skill in development mode
- User asks you to "initialize the backlog skill" or "set up kano-backlog"
- You need to help set up a local-first backlog system

## Installation: Development Mode

When working with a cloned repository, install in **editable mode** so changes to the code take effect immediately.

### Step 0: Create Virtual Environment (Strongly Recommended)

**IMPORTANT:** Always use a virtual environment to avoid conflicts with system Python packages.

**Windows (PowerShell):**
```powershell
# Create venv
python -m venv .venv

# Activate venv
.\.venv\Scripts\Activate.ps1

# Verify you're in venv (should show .venv path)
where.exe python
```

**Linux/macOS (Bash):**
```bash
# Create venv
python -m venv .venv

# Activate venv
source .venv/bin/activate

# Verify you're in venv (should show .venv path)
which python
```

### Step 1: Verify Prerequisites

```bash
# Check Python version (must be 3.8+)
python --version

# Verify you're in a virtual environment (CRITICAL)
# Windows: where.exe python
# Linux/macOS: which python
# Should show .venv path, NOT system Python
```

### Step 2: Install in Editable Mode

```bash
# Navigate to the skill directory
cd skills/kano-agent-backlog-skill

# Install with dev dependencies
pip install -e ".[dev]"

# This installs:
# - The kano-backlog CLI command
# - All runtime dependencies
# - Development tools (pytest, black, isort, mypy)
```

**What `-e` (editable mode) does:**
- Creates a link to the source code instead of copying files
- Code changes take effect immediately without reinstalling
- Perfect for development and testing

### Step 3: Verify Installation

```bash
# Check CLI is available
bash scripts/internal/show-version.sh
# Expected output: kob version from VERSION

# Inspect command surface
kob

# Run environment check
kob doctor
# All checks should pass (✅)
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
kob admin init --product <product-name> --agent <agent-id>

# Example:
kob admin init --product my-app --agent kiro
```

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
kob item create \
  --type task \
  --title "Implement user authentication" \
  --product my-app \
  --agent kiro

# Output: Created task: MYAPP-TSK-0001
```

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
kob workitem set-ready MYAPP-TSK-0001 \
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
kob workitem update-state MYAPP-TSK-0001 \
  --state InProgress \
  --product my-app

# Complete work
kob workitem update-state MYAPP-TSK-0001 \
  --state Done \
  --product my-app
```

### Recording Decisions

**Create an ADR for significant decisions:**

```bash
kob adr create \
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
kob item create --type task --title "My task" --product my-app --agent kiro

# ❌ Wrong - missing --agent
kob item create --type task --title "My task" --product my-app
```

## Troubleshooting

### Windows: "ModuleNotFoundError: No module named 'kano_backlog_cli'"

**Problem:** The legacy `kano-backlog.exe` packaging path may differ from the preferred repo-local kob surface in some Windows environments

**Solution (Recommended):** Prefer the repo-local kob launcher and native build flow:

```powershell
# Build the native CLI
bash src/cpp/build/script/windows/build_windows_ninja_msvc_debug.sh

# Then use the local launcher
./kob item create --type task --title "My task" --product my-app --agent kiro
```

**Why this happens:**
- The packaged Python wrapper can differ from the preferred repo-local native surface
- The repo-local `kob` launcher goes straight to the current native build output

**Alternative:** Reinstall in a clean venv:
```powershell
# Deactivate current venv
deactivate

# Remove old venv
Remove-Item -Recurse -Force .venv

# Create fresh venv
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Reinstall
cd skills/kano-agent-backlog-skill
pip install -e ".[dev]"
```

### "kob: command not found"

**Problem:** CLI not in PATH after installation

**Solution:**
```bash
# Verify installation
pip show kano-agent-backlog-skill

# If installed but not in PATH, use the repo-local launcher:
bash scripts/internal/show-version.sh
kob

# Or reinstall:
pip uninstall kano-agent-backlog-skill
pip install -e ".[dev]"
```

### "Missing --agent parameter"

**Problem:** Command fails with error about missing `--agent` parameter

**Solution:** The `--agent` flag is **REQUIRED** for all commands that modify the backlog:

```bash
# ❌ Wrong - will fail
kob item create --type task --title "My task" --product my-app

# ✅ Correct - includes --agent
kob item create --type task --title "My task" --product my-app --agent kiro
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

### "No module named 'kano_backlog_core'"

**Problem:** Package not installed or installed incorrectly

**Solution:**
```bash
# Ensure you're in the skill directory
cd skills/kano-agent-backlog-skill

# Reinstall in editable mode
pip install -e ".[dev]"
```

### "Invalid state transition"

**Problem:** Trying to skip required states (e.g., Proposed → Done)

**Solution:**
```bash
# Follow the state machine:
# Proposed → Planned → Ready → InProgress → Done

# Move through states sequentially:
kob workitem update-state <ID> --state Planned --product <product>
kob workitem set-ready <ID> --product <product> --context "..." --goal "..." --approach "..." --acceptance-criteria "..." --risks "..."
kob workitem update-state <ID> --state InProgress --product <product>
kob workitem update-state <ID> --state Done --product <product>
```

### "Ready gate validation failed"

**Problem:** Task/Bug missing required fields

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
pip install -e ".[dev]"
bash scripts/internal/show-version.sh
kob
kob doctor
```

### Initialization
```bash
cd /path/to/project
kob admin init --product <product> --agent <agent>
```

### Common Commands
```bash
# Create item
kob item create --type task --title "<title>" --product <product> --agent <agent>

# List items
kob item list --product <product>

# Update state
kob workitem update-state <ID> --state <state> --product <product>

# Create ADR
kob adr create --title "<title>" --product <product> --agent <agent>

# Check environment
kob doctor
```

## For Users: Installing from PyPI

If the user wants to install the released version instead of development mode:

```bash
# Install from PyPI (when available)
pip install kano-agent-backlog-skill

# Verify
bash scripts/internal/show-version.sh
kob
kob doctor
```

See [Quick Start Guide](quick-start.md) for the standard installation workflow.

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
- **Always use a virtual environment** (`.venv`) to avoid package conflicts
- **Always provide explicit `--agent` flags** for auditability
- **On Windows, if you encounter module or wrapper issues**, prefer the repo-local `kob` launcher after running the native build step
- Install with `pip install -e ".[dev]"` for development mode
