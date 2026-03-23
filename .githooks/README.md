# Git Hooks for Backlog Discipline

This directory contains git hooks to help maintain backlog discipline by reminding developers to create/update backlog items.

## Installation

Enable these hooks in your repository:

```bash
cd skills/kano-agent-backlog-skill
git config core.hooksPath .githooks
```

## Available Hooks

### 1. commit-msg (Warning)

**When**: Before commit is finalized
**Action**: Warns if commit message doesn't contain a backlog item ID

**Example output:**
```
⚠️  WARNING: No backlog item ID found in commit message

   This commit appears to be a feature/fix/refactor without a ticket.

   Recommended actions:
   1. Create a backlog item:
      kob item create --type task --title "..."

   2. Update commit message to include ticket ID:
      git commit --amend -m "KABSD-TSK-XXXX: your message"

   3. Or add to existing ticket's worklog:
      kob worklog append KABSD-TSK-XXXX --message "..."

   (This is a warning only - commit will proceed)
```

**Exceptions** (no warning):
- Commits with ticket IDs: `KABSD-TSK-0123: fix bug`
- Trivial commits: `docs: update README`, `chore: bump version`
- Merge commits: `Merge branch 'main'`
- Revert commits: `Revert "bad commit"`

### 2. post-commit (Reminder)

**When**: After commit is created
**Action**: Analyzes commit and suggests creating appropriate backlog item

**Example output:**
```
======================================================================
📋 BACKLOG REMINDER: No ticket ID found in commit
======================================================================

Commit: a1b2c3d
Message: Add binary vector storage support...
Files changed: 3

💡 Suggested action: Create a TASK item

Quick commands:

  # Create task
  kob item create \
    --type task \
    --title "Feature implementation" \
    --product kano-agent-backlog-skill \
    --agent $(whoami)

  # Or add to existing ticket
  kob worklog append KABSD-TSK-XXXX \
    --message "Commit a1b2c3d: ..." \
    --agent $(whoami) \
    --product kano-agent-backlog-skill

  # Then amend commit message
  git commit --amend -m "KABSD-TSK-XXXX: your message"

======================================================================
```

**Smart suggestions:**
- Detects `feat:` → suggests Task
- Detects `fix:` → suggests Bug
- Detects `refactor:` → suggests Task (refactoring)
- Detects test files → suggests Task (test implementation)

## Workflow Examples

### Example 1: Feature Development

```bash
# 1. Make changes
vim src/feature.py

# 2. Commit (without ticket)
git commit -m "feat: add new feature"

# Hook warns you!
⚠️  WARNING: No backlog item ID found in commit message

# 3. Create ticket
kob item create --type task --title "Add new feature"
# Output: Created KABSD-TSK-0317

# 4. Amend commit
git commit --amend -m "KABSD-TSK-0317: add new feature"
```

### Example 2: Bug Fix

```bash
# 1. Fix bug
vim src/buggy.py

# 2. Commit
git commit -m "fix: resolve crash on startup"

# Hook suggests creating a Bug item
📋 BACKLOG REMINDER: No ticket ID found in commit
💡 Suggested action: Create a BUG item

# 3. Create bug ticket
kob item create --type bug --title "Crash on startup"
# Output: Created KABSD-BUG-0042

# 4. Amend commit
git commit --amend -m "KABSD-BUG-0042: resolve crash on startup"
```

### Example 3: Trivial Change (No Warning)

```bash
# Trivial commits don't trigger warnings
git commit -m "docs: fix typo in README"
git commit -m "chore: bump version to 0.0.3"
git commit -m "style: format code"

# No warning - these don't need tickets
```

## Disabling Hooks

### Temporarily (one commit)

```bash
git commit --no-verify -m "emergency fix"
```

### Permanently (not recommended)

```bash
git config --unset core.hooksPath
```

## Customization

### Adjust Warning Level

Edit `.githooks/commit-msg` to change behavior:

```bash
# Make it an error (block commit)
exit 1  # instead of exit 0

# Disable for certain branches
if [ "$(git branch --show-current)" = "experimental" ]; then
    exit 0
fi
```

### Add Custom Patterns

Edit `.githooks/post-commit` to add custom detection:

```python
# Add custom ticket type detection
if 'security' in msg:
    return 'bug', 'Security fix'
```

## Best Practices

### 1. Create Ticket First (Ideal)

```bash
# 1. Create ticket
kob item create --type task --title "Add feature X"
# Output: KABSD-TSK-0318

# 2. Work on it
vim src/feature.py

# 3. Commit with ticket ID
git commit -m "KABSD-TSK-0318: implement feature X"

# No warnings!
```

### 2. Create Ticket After (Acceptable)

```bash
# 1. Work and commit
git commit -m "feat: add feature X"

# Hook warns you

# 2. Create ticket immediately
kob item create --type task --title "Add feature X"

# 3. Amend commit
git commit --amend -m "KABSD-TSK-0318: add feature X"
```

### 3. Batch Ticket Creation (End of Day)

```bash
# Review today's commits without tickets
git log --oneline --since="today" | grep -v "KABSD-"

# Create tickets for each
kob item create --type task --title "..."

# Update commit messages
git rebase -i HEAD~5  # Interactive rebase to update messages
```

## Integration with CI/CD

### GitHub Actions

```yaml
name: Check Backlog Discipline

on: [pull_request]

jobs:
  check-tickets:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Check commit messages
        run: |
          git log origin/main..HEAD --pretty=%s | while read msg; do
            if ! echo "$msg" | grep -qE "KABSD-|^(docs|chore|style):"; then
              echo "❌ Commit without ticket: $msg"
              exit 1
            fi
          done
```

### Pre-push Hook

```bash
# .githooks/pre-push
# Check all commits being pushed have ticket IDs

while read local_ref local_sha remote_ref remote_sha; do
    if [ "$local_sha" != "0000000000000000000000000000000000000000" ]; then
        commits=$(git log "$remote_sha..$local_sha" --pretty=%s)
        
        while IFS= read -r msg; do
            if ! echo "$msg" | grep -qE "KABSD-|^(docs|chore|style):"; then
                echo "❌ Cannot push: Commit without ticket: $msg"
                exit 1
            fi
        done <<< "$commits"
    fi
done

exit 0
```

## Troubleshooting

### Hook not running

```bash
# Check if hooks are enabled
git config core.hooksPath
# Should output: .githooks

# Check if hook is executable
ls -la .githooks/
# Should show: -rwxr-xr-x

# Make executable if needed
chmod +x .githooks/*
```

### False positives

If hooks warn on commits that shouldn't need tickets, add patterns to the exception list in the hook scripts.

## Philosophy

These hooks follow the principle:

> **"Gentle reminders, not strict enforcement"**

- **Soft reminders**, not warnings or errors
- **Always allow commits** to proceed
- **Easy to disable** (one command)
- **Smart detection** (trivial commits exempted)
- **Helpful suggestions** (copy-paste commands)

The goal is to **build good habits**, not to **block productivity**.

### Why Soft Reminders?

**Some commits don't need tickets:**
- Exploratory work (trying things out)
- WIP commits (work in progress)
- Quick fixes (typos, formatting)
- Experimental branches
- Personal projects

**The hook respects this:**
- Shows a gentle "💡 Reminder" (not "⚠️ WARNING")
- Emphasizes it's optional
- Easy to disable per-repo or globally
- Never blocks your work

### Disabling Reminders

**Per repository:**
```bash
git config kano.backlog.reminders false
```

**Globally (all repos):**
```bash
git config --global kano.backlog.reminders false
```

**Re-enable:**
```bash
git config kano.backlog.reminders true
```

**Check status:**
```bash
git config --get kano.backlog.reminders
# (empty) = enabled (default)
# false = disabled
# true = explicitly enabled
```
