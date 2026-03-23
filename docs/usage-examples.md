# Usage Examples

**Complete command reference with practical examples for kano-agent-backlog-skill**

This guide provides comprehensive examples for each major CLI command group, showing common workflows and expected outputs. All examples assume you have installed the package via `pip install kano-agent-backlog-skill`.

## Table of Contents

- [Admin Commands](#admin-commands)
- [Item/Workitem Commands](#itemworkitem-commands)
- [State Commands](#state-commands)
- [Worklog Commands](#worklog-commands)
- [ADR Commands](#adr-commands)
- [Topic Commands](#topic-commands)
- [Workset Commands](#workset-commands)
- [View Commands](#view-commands)
- [Config Commands](#config-commands)
- [Search Commands](#search-commands)
- [Doctor Command](#doctor-command)
- [Common Workflows](#common-workflows)

---

## Admin Commands

Administrative and setup commands for backlog initialization and maintenance.

### Initialize a New Backlog

**Command:**
```bash
kob admin init --product my-app --agent kiro
```

**Expected Output:**
```
✓ Created backlog root: _kano/backlog/
✓ Created product directory: _kano/backlog/products/my-app/
✓ Created item type directories (epic, feature, user_story, task, bug)
✓ Created decisions directory
✓ Created _meta directory with indexes
✓ Initialized product config
✓ Backlog initialized successfully for product: my-app
```

**What It Does:**
- Creates the complete directory structure under `_kano/backlog/`
- Sets up product-specific directories and configuration
- Initializes metadata files for tracking sequences and indexes


### Sync Sequence Numbers

**Command:**
```bash
kob admin sync-sequences --product my-app
```

**Expected Output:**
```
Scanning items in: _kano/backlog/items/
Found 42 items across 5 types
✓ Synced sequences: EPIC=5, FEATURE=12, STORY=8, TASK=15, BUG=2
✓ Updated _meta/sequences.json
```

**When to Use:**
- After manually creating or deleting item files
- When sequence numbers appear out of sync
- During backlog maintenance or cleanup

---

## Item/Workitem Commands

Create and manage work items (Epic, Feature, User Story, Task, Bug).

### Create an Epic

**Command:**
```bash
kob item create --type epic \
  --title "User Authentication System" \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Created Epic: MYAPP-EPC-0001
  File: _kano/backlog/items/epic/0000/MYAPP-EPC-0001_user-authentication-system.md
  Title: User Authentication System
  State: Proposed
```

**File Contents:**
```markdown
---
id: MYAPP-EPC-0001
type: Epic
title: User Authentication System
state: Proposed
product: my-app
created: 2025-01-26T10:30:00Z
updated: 2025-01-26T10:30:00Z
---

# User Authentication System

## Context

<!-- Why is this epic needed? What problem does it solve? -->

## Goal

<!-- What does success look like for this epic? -->

## Worklog

2025-01-26 10:30 [agent=kiro] Created Epic
```


### Create a Feature

**Command:**
```bash
kob item create --type feature \
  --title "JWT Token Authentication" \
  --parent MYAPP-EPC-0001 \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Created Feature: MYAPP-FTR-0001
  File: _kano/backlog/items/feature/0000/MYAPP-FTR-0001_jwt-token-authentication.md
  Title: JWT Token Authentication
  Parent: MYAPP-EPC-0001
  State: Proposed
```

### Create a Task

**Command:**
```bash
kob item create --type task \
  --title "Implement JWT token generation" \
  --parent MYAPP-FTR-0001 \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Created Task: MYAPP-TSK-0001
  File: _kano/backlog/items/task/0000/MYAPP-TSK-0001_implement-jwt-token-generation.md
  Title: Implement JWT token generation
  Parent: MYAPP-FTR-0001
  State: Proposed
```

### Create a Bug

**Command:**
```bash
kob item create --type bug \
  --title "Login fails with special characters in password" \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Created Bug: MYAPP-BUG-0001
  File: _kano/backlog/items/bug/0000/MYAPP-BUG-0001_login-fails-with-special-characters.md
  Title: Login fails with special characters in password
  State: Proposed
```

### List Items

**Command:**
```bash
kob item list --product my-app
```

**Expected Output:**
```
Items in product: my-app

Epics (1):
  MYAPP-EPC-0001  User Authentication System  [Proposed]

Features (1):
  MYAPP-FTR-0001  JWT Token Authentication  [Proposed]  (parent: MYAPP-EPC-0001)

Tasks (1):
  MYAPP-TSK-0001  Implement JWT token generation  [Proposed]  (parent: MYAPP-FTR-0001)

Bugs (1):
  MYAPP-BUG-0001  Login fails with special characters  [Proposed]

Total: 4 items
```

### List Items by State

**Command:**
```bash
kob item list --product my-app --state Ready
```

**Expected Output:**
```
Items in state: Ready

Tasks (2):
  MYAPP-TSK-0001  Implement JWT token generation  [Ready]
  MYAPP-TSK-0002  Add password hashing with bcrypt  [Ready]

Total: 2 items
```


### Set Task to Ready State

**Command:**
```bash
kob workitem set-ready MYAPP-TSK-0001 --product my-app \
  --context "Users need secure authentication tokens for API access" \
  --goal "Generate JWT tokens with user claims and expiration" \
  --approach "Use PyJWT library with RS256 algorithm and 1-hour expiration" \
  --acceptance-criteria "Token contains user_id, email, exp claims; validates correctly; expires after 1 hour" \
  --risks "Key management needs secure storage; token refresh not in scope"
```

**Expected Output:**
```
✓ Task MYAPP-TSK-0001 is now Ready
✓ All required fields validated:
  - Context: ✓
  - Goal: ✓
  - Approach: ✓
  - Acceptance Criteria: ✓
  - Risks / Dependencies: ✓
✓ Worklog entry added
```

**What It Does:**
- Validates all required fields are non-empty
- Updates item state to "Ready"
- Appends worklog entry with timestamp and agent
- Enforces the Ready gate before work can begin

---

## State Commands

Manage item state transitions.

### Update Item State

**Command:**
```bash
kob workitem update-state MYAPP-TSK-0001 --state InProgress \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Updated state: Proposed → InProgress
✓ Worklog entry added: 2025-01-26 11:00 [agent=kiro] State: Proposed → InProgress
```

### Valid State Transitions

**State Machine:**
```
Proposed → Planned → Ready → InProgress → Done
    ↓         ↓        ↓          ↓
  Dropped   Dropped  Blocked   Blocked
```

**Example Transitions:**
```bash
# Move to Planned
kob workitem update-state MYAPP-TSK-0001 --state Planned --agent kiro --product my-app

# Move to Ready (requires Ready gate fields)
kob workitem set-ready MYAPP-TSK-0001 --product my-app --context "..." --goal "..." --approach "..." --acceptance-criteria "..." --risks "..."

# Start work
kob workitem update-state MYAPP-TSK-0001 --state InProgress --agent kiro --product my-app

# Mark as blocked
kob workitem update-state MYAPP-TSK-0001 --state Blocked --agent kiro --product my-app

# Complete work
kob workitem update-state MYAPP-TSK-0001 --state Done --agent kiro --product my-app

# Drop item
kob workitem update-state MYAPP-TSK-0001 --state Dropped --agent kiro --product my-app
```

---

## Worklog Commands

Append worklog entries to track decisions and progress.

### Add Worklog Entry

**Command:**
```bash
kob worklog append MYAPP-TSK-0001 \
  --entry "Decided to use RS256 instead of HS256 for better key management" \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Worklog entry added to MYAPP-TSK-0001
  2025-01-26 11:15 [agent=kiro] Decided to use RS256 instead of HS256 for better key management
```

**When to Use:**
- Recording load-bearing decisions
- Documenting approach changes
- Noting blockers or dependencies discovered
- Capturing context that affects implementation


---

## ADR Commands

Create and manage Architecture Decision Records.

### Create an ADR

**Command:**
```bash
kob adr create \
  --title "Use JWT with RS256 for API authentication" \
  --product my-app --agent kiro
```

**Expected Output:**
```
✓ Created ADR: MYAPP-ADR-0001
  File: _kano/backlog/decisions/MYAPP-ADR-0001_use-jwt-with-rs256.md
  Title: Use JWT with RS256 for API authentication
  Status: Proposed
```

**File Contents:**
```markdown
---
id: MYAPP-ADR-0001
title: Use JWT with RS256 for API authentication
status: Proposed
date: 2025-01-26
product: my-app
---

# Use JWT with RS256 for API authentication

## Status

Proposed

## Context

<!-- What is the issue that we're seeing that is motivating this decision or change? -->

## Decision

<!-- What is the change that we're proposing and/or doing? -->

## Consequences

<!-- What becomes easier or more difficult to do because of this change? -->

## Alternatives Considered

<!-- What other options were evaluated? Why were they not chosen? -->

## Related Items

<!-- Link to related work items, features, or other ADRs -->

## Worklog

2025-01-26 10:45 [agent=kiro] Created ADR
```

### Link ADR to Work Item

**Command:**
```bash
kob worklog append MYAPP-TSK-0001 \
  --entry "Linked ADR-0001: Use JWT with RS256 for API authentication" \
  --agent kiro --product my-app
```

**Expected Output:**
```
✓ Worklog entry added to MYAPP-TSK-0001
  2025-01-26 11:20 [agent=kiro] Linked ADR-0001: Use JWT with RS256 for API authentication
```

---

## Topic Commands

Context grouping for rapid focus area changes.

### Create a Topic

**Command:**
```bash
kob topic create auth-refactor --agent kiro
```

**Expected Output:**
```
✓ Created topic: auth-refactor
  Directory: _kano/backlog/topics/auth-refactor/
  Files created:
    - metadata.json
    - brief.md
    - materials/
    - snapshots/
```

### Add Items to Topic

**Command:**
```bash
kob topic add auth-refactor --item MYAPP-TSK-0001
kob topic add auth-refactor --item MYAPP-BUG-0001
```

**Expected Output:**
```
✓ Added MYAPP-TSK-0001 to topic: auth-refactor
✓ Added MYAPP-BUG-0001 to topic: auth-refactor
```

### Pin Documents to Topic

**Command:**
```bash
kob topic pin auth-refactor \
  --doc _kano/backlog/decisions/MYAPP-ADR-0001_use-jwt-with-rs256.md
```

**Expected Output:**
```
✓ Pinned document to topic: auth-refactor
  Document: MYAPP-ADR-0001_use-jwt-with-rs256.md
```

### Add Code Snippet to Topic

**Command:**
```bash
kob topic add-snippet auth-refactor \
  --file src/auth/jwt.py --start 10 --end 25 \
  --agent kiro
```

**Expected Output:**
```
✓ Added code snippet to topic: auth-refactor
  File: src/auth/jwt.py (lines 10-25)
  Saved to: _kano/backlog/topics/auth-refactor/materials/snippets/jwt_py_10_25.md
```


### Distill Topic Brief

**Command:**
```bash
kob topic distill auth-refactor
```

**Expected Output:**
```
✓ Distilled brief for topic: auth-refactor
  Output: _kano/backlog/topics/auth-refactor/brief.md
  Included:
    - 2 work items
    - 1 pinned document
    - 1 code snippet
```

### Switch Active Topic

**Command:**
```bash
kob topic switch auth-refactor --agent kiro
```

**Expected Output:**
```
✓ Switched to topic: auth-refactor
  Previous topic: (none)
  Active items: 2
  Pinned docs: 1
```

### Export Topic Context

**Command:**
```bash
kob topic export-context auth-refactor --format json
```

**Expected Output:**
```json
{
  "topic_id": "auth-refactor",
  "created": "2025-01-26T10:00:00Z",
  "items": [
    {
      "id": "MYAPP-TSK-0001",
      "title": "Implement JWT token generation",
      "state": "InProgress"
    },
    {
      "id": "MYAPP-BUG-0001",
      "title": "Login fails with special characters",
      "state": "Ready"
    }
  ],
  "documents": [
    {
      "path": "_kano/backlog/decisions/MYAPP-ADR-0001_use-jwt-with-rs256.md",
      "type": "adr"
    }
  ],
  "snippets": [
    {
      "file": "src/auth/jwt.py",
      "lines": "10-25"
    }
  ]
}
```

### Close Topic

**Command:**
```bash
kob topic close auth-refactor --agent kiro
```

**Expected Output:**
```
✓ Closed topic: auth-refactor
  Status: closed
  Closed at: 2025-01-26T12:00:00Z
```

### Cleanup Old Topics

**Command:**
```bash
kob topic cleanup --ttl-days 14 --apply
```

**Expected Output:**
```
Scanning topics older than 14 days...
Found 3 topics eligible for cleanup:
  - old-feature-work (closed 20 days ago)
  - experimental-api (closed 18 days ago)
  - refactor-attempt (closed 15 days ago)

✓ Cleaned up 3 topics
```

---

## Workset Commands

Per-item execution cache to prevent agent drift.

### Initialize Workset

**Command:**
```bash
kob workset init --item MYAPP-TSK-0001 --agent kiro
```

**Expected Output:**
```
✓ Initialized workset for MYAPP-TSK-0001
  Directory: _kano/backlog/worksets/MYAPP-TSK-0001/
  Files created:
    - plan.md (derived from acceptance criteria)
    - notes.md (working notes template)
    - deliverables/ (staging area)
```

**plan.md Contents:**
```markdown
# Execution Plan: MYAPP-TSK-0001

## Checklist

- [ ] Token contains user_id claim
- [ ] Token contains email claim
- [ ] Token contains exp claim
- [ ] Token validates correctly
- [ ] Token expires after 1 hour

## Notes

<!-- Track progress and decisions here -->
```


### Get Next Action from Plan

**Command:**
```bash
kob workset next --item MYAPP-TSK-0001
```

**Expected Output:**
```
Next action for MYAPP-TSK-0001:

[ ] Token contains user_id claim

Remaining: 5 items
Completed: 0 items
```

### Detect ADR Candidates

**Command:**
```bash
kob workset detect-adr --item MYAPP-TSK-0001
```

**Expected Output:**
```
Scanning notes.md for decision markers...

Found 2 potential ADR candidates:
  1. "Decided to use RS256 instead of HS256 for better key management"
  2. "Chose 1-hour expiration to balance security and UX"

Recommendation: Create ADRs for significant architectural decisions
```

### Promote Deliverables

**Command:**
```bash
kob workset promote --item MYAPP-TSK-0001 --agent kiro
```

**Expected Output:**
```
Promoting deliverables from workset to canonical locations...

✓ Moved: deliverables/jwt_generator.py → src/auth/jwt_generator.py
✓ Moved: deliverables/test_jwt.py → tests/auth/test_jwt.py
✓ Updated worklog in MYAPP-TSK-0001

Promoted 2 files
```

### Cleanup Expired Worksets

**Command:**
```bash
kob workset cleanup --ttl-hours 72 --apply
```

**Expected Output:**
```
Scanning worksets older than 72 hours...
Found 4 expired worksets:
  - MYAPP-TSK-0005 (completed 5 days ago)
  - MYAPP-TSK-0008 (completed 4 days ago)
  - MYAPP-BUG-0002 (completed 3 days ago)
  - MYAPP-TSK-0012 (abandoned 6 days ago)

✓ Cleaned up 4 worksets
```

---

## View Commands

Generate dashboards and reports.

### Refresh Product Views

**Command:**
```bash
kob view refresh --product my-app --agent kiro
```

**Expected Output:**
```
Generating views for product: my-app

✓ Generated: _kano/backlog/products/my-app/views/Dashboard.md
✓ Generated: _kano/backlog/products/my-app/views/ByState.md
✓ Generated: _kano/backlog/products/my-app/views/ByType.md
✓ Generated: _kano/backlog/products/my-app/views/RecentActivity.md

4 views generated
```

### Generate Snapshot Report

**Command:**
```bash
kob topic snapshot create my-topic dev-snapshot --agent kiro
```

**Expected Output:**
```
Creating snapshot for product: my-app

✓ Snapshot created: _kano/backlog/products/my-app/snapshots/snapshot_2025-01-26_120000.md
  Items included: 42
  ADRs included: 5
  Format: developer
```

**Snapshot Contents:**
```markdown
# Backlog Snapshot: my-app
Generated: 2025-01-26 12:00:00

## Summary
- Total Items: 42
- By State: Ready (8), InProgress (5), Done (25), Blocked (2), Proposed (2)
- By Type: Epic (3), Feature (10), Task (25), Bug (4)

## Active Work (InProgress)
- MYAPP-TSK-0001: Implement JWT token generation
- MYAPP-TSK-0003: Add password hashing with bcrypt
- MYAPP-TSK-0007: Create user registration endpoint
- MYAPP-BUG-0001: Login fails with special characters
- MYAPP-TSK-0015: Write integration tests for auth flow

## Ready for Work
- MYAPP-TSK-0002: Implement token refresh mechanism
- MYAPP-TSK-0004: Add rate limiting to login endpoint
...
```


---

## Config Commands

Inspect and validate layered configuration.

### Show Effective Configuration

**Command:**
```bash
kob config show
```

**Expected Output:**
```yaml
# Effective Configuration (merged from all layers)

backlog:
  root: _kano/backlog
  default_product: my-app

cache:
  root: .kano/cache/backlog

views:
  auto_refresh: true
  formats:
    - obsidian
    - markdown

tokenizer:
  default_adapter: openai
  cache_enabled: true

embedding:
  model: all-MiniLM-L6-v2
  batch_size: 32

# Configuration sources:
# 1. Default config (built-in)
# 2. User config: ~/.kano/backlog_config.toml
# 3. Project config: .kano/backlog_config.toml
# 4. Profile: .kano/backlog_config/production.toml
```

### Validate Configuration

**Command:**
```bash
kob config validate
```

**Expected Output:**
```
Validating configuration...

✓ Syntax: Valid TOML
✓ Required fields: All present
✓ Backlog root: Exists and writable
✓ Cache root: Exists and writable
✓ Product config: Valid
✓ Tokenizer config: Valid

Configuration is valid
```

### Export Configuration

**Command:**
```bash
kob config show --product my-app
```

**Expected Output:**
```
✓ Exported configuration to: config.json
  Format: JSON
  Size: 2.4 KB
```

---

## Search Commands

Vector similarity search (requires optional dependencies).

### Build Search Index

**Command:**
```bash
kob embedding build --product my-app
```

**Expected Output:**
```
Building embedding index for product: my-app

Scanning items...
Found 42 items to index

Processing items:
  [████████████████████████████████] 42/42 (100%)

✓ Index built successfully
  Items indexed: 42
  Vectors generated: 42
  Index file: .kano/cache/backlog/my-app/embeddings.faiss
  Metadata file: .kano/cache/backlog/my-app/metadata.json
  Time: 12.3s
```

### Search Items

**Command:**
```bash
kob search query "authentication token generation" --product my-app
```

**Expected Output:**
```
Search results for: "authentication token generation"

1. MYAPP-TSK-0001: Implement JWT token generation (similarity: 0.92)
   State: InProgress
   Context: Users need secure authentication tokens for API access
   
2. MYAPP-TSK-0002: Implement token refresh mechanism (similarity: 0.85)
   State: Ready
   Context: Tokens need to be refreshable without re-authentication
   
3. MYAPP-ADR-0001: Use JWT with RS256 for API authentication (similarity: 0.78)
   Status: Accepted
   Decision: Use JWT tokens with RS256 algorithm for API authentication

Found 3 results
```

### Check Index Status

**Command:**
```bash
kob embedding status --product my-app
```

**Expected Output:**
```
Embedding index status for product: my-app

Index file: .kano/cache/backlog/my-app/embeddings.faiss
Status: ✓ Up to date
Items indexed: 42
Last updated: 2025-01-26 11:30:00
Model: all-MiniLM-L6-v2
Dimension: 384

Stale items: 0
New items: 0
```


---

## Doctor Command

Environment and backlog health checks.

### Run Doctor Checks

**Command:**
```bash
kob doctor
```

**Expected Output:**
```
Running environment checks...

✓ Python version: 3.11.5 (>= 3.8 required)
✓ SQLite version: 3.42.0 (>= 3.8 required)
✓ Backlog root: _kano/backlog/ (exists and writable)
✓ Product config: Valid
✓ Required directories: All present
✓ Sequence files: Valid

Optional dependencies:
  ✓ [dev] pytest, black, mypy, isort (installed)
  ⚠ [vector] sentence-transformers, faiss-cpu (not installed)
    Install with: pip install kano-agent-backlog-skill[vector]

Recommendations:
  - Consider installing [vector] dependencies for semantic search
  - Run 'kob admin sync-sequences --product my-app' to ensure sequence numbers are current

Overall status: ✓ Healthy
```

### Doctor with Issues

**Expected Output (with problems):**
```
Running environment checks...

✓ Python version: 3.11.5 (>= 3.8 required)
✓ SQLite version: 3.42.0 (>= 3.8 required)
❌ Backlog root: _kano/backlog/ (not found)
  → Run: kob admin init --product <name> --agent <agent>

⚠ Product config: Missing required field 'default_product'
  → Add to .kano/backlog_config.toml:
    [backlog]
    default_product = "my-app"

❌ Required directories: Missing 2 directories
  - _kano/backlog/items/task/
  - _kano/backlog/decisions/
  → Run: kob admin init --product <name> --agent <agent>

Overall status: ❌ Issues found (2 critical, 1 warning)
```

---

## Common Workflows

### Workflow 1: Create Epic → Feature → Task

**Complete workflow from planning to execution:**

```bash
# 1. Initialize backlog (first time only)
kob admin init --product my-app --agent kiro

# 2. Create Epic
kob item create --type epic \
  --title "User Authentication System" \
  --agent kiro --product my-app
# Output: Created MYAPP-EPC-0001

# 3. Create Feature under Epic
kob item create --type feature \
  --title "JWT Token Authentication" \
  --parent MYAPP-EPC-0001 \
  --agent kiro --product my-app
# Output: Created MYAPP-FTR-0001

# 4. Create Task under Feature
kob item create --type task \
  --title "Implement JWT token generation" \
  --parent MYAPP-FTR-0001 \
  --agent kiro --product my-app
# Output: Created MYAPP-TSK-0001

# 5. Set Task to Ready (enforces required fields)
kob workitem set-ready MYAPP-TSK-0001 --product my-app \
  --context "Users need secure authentication tokens for API access" \
  --goal "Generate JWT tokens with user claims and expiration" \
  --approach "Use PyJWT library with RS256 algorithm and 1-hour expiration" \
  --acceptance-criteria "Token contains user_id, email, exp claims; validates correctly; expires after 1 hour" \
  --risks "Key management needs secure storage; token refresh not in scope"
# Output: Task is now Ready

# 6. Initialize workset to prevent drift
kob workset init --item MYAPP-TSK-0001 --agent kiro
# Output: Workset initialized with plan.md

# 7. Start work
kob workitem update-state MYAPP-TSK-0001 --state InProgress \
  --agent kiro --product my-app
# Output: State updated to InProgress

# 8. Get next action from plan
kob workset next --item MYAPP-TSK-0001
# Output: Next unchecked item from plan

# 9. Record decisions in worklog
kob worklog append MYAPP-TSK-0001 \
  --entry "Decided to use RS256 instead of HS256 for better key management" \
  --agent kiro --product my-app
# Output: Worklog entry added

# 10. Create ADR for significant decision
kob adr create \
  --title "Use JWT with RS256 for API authentication" \
  --product my-app --agent kiro
# Output: Created MYAPP-ADR-0001

# 11. Link ADR to task
kob worklog append MYAPP-TSK-0001 \
  --entry "Linked ADR-0001: Use JWT with RS256 for API authentication" \
  --agent kiro --product my-app
# Output: Worklog entry added

# 12. Promote deliverables when done
kob workset promote --item MYAPP-TSK-0001 --agent kiro
# Output: Files moved to canonical locations

# 13. Complete task
kob workitem update-state MYAPP-TSK-0001 --state Done \
  --agent kiro --product my-app
# Output: State updated to Done

# 14. Refresh views
kob view refresh --product my-app --agent kiro
# Output: Views regenerated
```


### Workflow 2: Bug Triage and Fix

**Complete workflow for handling a bug:**

```bash
# 1. Create bug report
kob item create --type bug \
  --title "Login fails with special characters in password" \
  --agent kiro --product my-app
# Output: Created MYAPP-BUG-0001

# 2. Add initial investigation notes
kob worklog append MYAPP-BUG-0001 \
  --entry "Reproduced: passwords with @ or # fail validation" \
  --agent kiro --product my-app

# 3. Move to Planned for triage
kob workitem update-state MYAPP-BUG-0001 --state Planned \
  --agent kiro --product my-app

# 4. Set to Ready with fix approach
kob workitem set-ready MYAPP-BUG-0001 --product my-app \
  --context "Users cannot login with passwords containing special characters" \
  --goal "Allow all valid password characters including @, #, $, %" \
  --approach "Update password validation regex to allow special chars; add unit tests" \
  --acceptance-criteria "Users can login with passwords containing @#$%; existing passwords still work" \
  --risks "Need to ensure no SQL injection or XSS vulnerabilities"

# 5. Start work
kob workitem update-state MYAPP-BUG-0001 --state InProgress \
  --agent kiro --product my-app

# 6. Initialize workset
kob workset init --item MYAPP-BUG-0001 --agent kiro

# 7. Complete fix and update state
kob workitem update-state MYAPP-BUG-0001 --state Done \
  --agent kiro --product my-app

# 8. Add verification notes
kob worklog append MYAPP-BUG-0001 \
  --entry "Fixed: Updated regex pattern; added 15 test cases; all passing" \
  --agent kiro --product my-app
```

### Workflow 3: Context Switching with Topics

**Switching between different work areas:**

```bash
# 1. Create topic for current work
kob topic create auth-refactor --agent kiro

# 2. Add related items
kob topic add auth-refactor --item MYAPP-TSK-0001
kob topic add auth-refactor --item MYAPP-TSK-0002
kob topic add auth-refactor --item MYAPP-BUG-0001

# 3. Pin relevant documents
kob topic pin auth-refactor \
  --doc _kano/backlog/decisions/MYAPP-ADR-0001_use-jwt-with-rs256.md

# 4. Add code snippets for reference
kob topic add-snippet auth-refactor \
  --file src/auth/jwt.py --start 10 --end 50 --agent kiro

# 5. Distill brief for quick context
kob topic distill auth-refactor

# 6. Switch to this topic
kob topic switch auth-refactor --agent kiro

# --- Work on auth-refactor items ---

# 7. Need to switch to urgent bug fix
kob topic create urgent-bugfix --agent kiro
kob topic add urgent-bugfix --item MYAPP-BUG-0005
kob topic switch urgent-bugfix --agent kiro

# --- Work on urgent bug ---

# 8. Return to auth work
kob topic switch auth-refactor --agent kiro

# 9. Close topic when done
kob topic close auth-refactor --agent kiro

# 10. Export context for documentation
kob topic export-context auth-refactor --format markdown \
  --output docs/auth-refactor-summary.md
```

### Workflow 4: Multi-Agent Collaboration

**Handoff between agents:**

```bash
# Agent 1 (kiro) creates and plans work
kob item create --type task \
  --title "Add rate limiting to API endpoints" \
  --agent kiro --product my-app
# Output: Created MYAPP-TSK-0010

kob workitem set-ready MYAPP-TSK-0010 --product my-app \
  --context "API needs protection from abuse" \
  --goal "Implement rate limiting with Redis backend" \
  --approach "Use Flask-Limiter with Redis storage; 100 req/min per IP" \
  --acceptance-criteria "Rate limits enforced; 429 status returned; Redis stores counters" \
  --risks "Redis dependency; need fallback for Redis unavailability"

kob worklog append MYAPP-TSK-0010 \
  --entry "Planned approach: Flask-Limiter with Redis backend" \
  --agent kiro --product my-app

# Agent 2 (copilot) picks up and starts work
kob workitem update-state MYAPP-TSK-0010 --state InProgress \
  --agent copilot --product my-app

kob workset init --item MYAPP-TSK-0010 --agent copilot

kob worklog append MYAPP-TSK-0010 \
  --entry "Started implementation; installed Flask-Limiter" \
  --agent copilot --product my-app

# Agent 2 encounters blocker
kob workitem update-state MYAPP-TSK-0010 --state Blocked \
  --agent copilot --product my-app

kob worklog append MYAPP-TSK-0010 \
  --entry "Blocked: Redis connection config unclear; need DevOps input" \
  --agent copilot --product my-app

# Agent 3 (claude) resolves blocker
kob worklog append MYAPP-TSK-0010 \
  --entry "Unblocked: Redis config added to .env.example; using REDIS_URL env var" \
  --agent claude --product my-app

kob workitem update-state MYAPP-TSK-0010 --state InProgress \
  --agent claude --product my-app

# Agent 3 completes work
kob workset promote --item MYAPP-TSK-0010 --agent claude

kob workitem update-state MYAPP-TSK-0010 --state Done \
  --agent claude --product my-app

kob worklog append MYAPP-TSK-0010 \
  --entry "Completed: Rate limiting implemented; tests passing; docs updated" \
  --agent claude --product my-app
```


### Workflow 5: Search and Discovery

**Finding related work using semantic search:**

```bash
# 1. Build search index (first time or after many changes)
kob embedding build --product my-app
# Output: Index built with 42 items

# 2. Search for related work
kob search query "authentication security" --product my-app
# Output: 5 related items found

# 3. Search for specific topics
kob search query "password hashing bcrypt" --product my-app --limit 3
# Output: Top 3 most relevant items

# 4. Find items similar to a specific item
kob search similar MYAPP-TSK-0001 --product my-app
# Output: Items with similar context/goals

# 5. Check if index needs updating
kob embedding status --product my-app
# Output: Index status and stale items count

# 6. Rebuild index after many changes
kob embedding build --product my-app --force
# Output: Index rebuilt from scratch
```

---

## Tips and Best Practices

### 1. Always Use Agent Identity

**Good:**
```bash
kob item create --type task --title "..." --agent kiro --product my-app
```

**Bad:**
```bash
kob item create --type task --title "..." --product my-app
# Missing --agent flag; worklog won't track who created it
```

### 2. Enforce Ready Gate Before Starting Work

**Good workflow:**
```bash
# 1. Create task
kob item create --type task --title "..." --agent kiro --product my-app

# 2. Set to Ready with all required fields
kob workitem set-ready MYAPP-TSK-0001 --product my-app \
  --context "..." --goal "..." --approach "..." \
  --acceptance-criteria "..." --risks "..."

# 3. Start work
kob workitem update-state MYAPP-TSK-0001 --state InProgress --agent kiro --product my-app
```

**Bad workflow:**
```bash
# Skipping Ready gate - no context captured!
kob item create --type task --title "..." --agent kiro --product my-app
kob workitem update-state MYAPP-TSK-0001 --state InProgress --agent kiro --product my-app
```

### 3. Use Worksets to Prevent Drift

**Good:**
```bash
# Initialize workset with plan
kob workset init --item MYAPP-TSK-0001 --agent kiro

# Get next action
kob workset next --item MYAPP-TSK-0001

# Work on that specific action
# ...

# Get next action again
kob workset next --item MYAPP-TSK-0001
```

**Bad:**
```bash
# Working without a plan - easy to drift or forget requirements
kob workitem update-state MYAPP-TSK-0001 --state InProgress --agent kiro --product my-app
# ... implement whatever comes to mind ...
```

### 4. Record Decisions in Worklog

**Good:**
```bash
kob worklog append MYAPP-TSK-0001 \
  --entry "Decided to use Redis for rate limiting instead of in-memory cache for multi-instance support" \
  --agent kiro --product my-app
```

**Bad:**
```bash
# Making decisions without recording them - context lost forever
```

### 5. Create ADRs for Significant Decisions

**When to create an ADR:**
- Architectural choices (database, framework, protocol)
- Security decisions (authentication method, encryption)
- Performance trade-offs (caching strategy, optimization approach)
- API design decisions (REST vs GraphQL, versioning strategy)

**Example:**
```bash
kob adr create \
  --title "Use Redis for distributed rate limiting" \
  --product my-app --agent kiro

# Link to related work items
kob worklog append MYAPP-TSK-0010 \
  --entry "Linked ADR-0005: Use Redis for distributed rate limiting" \
  --agent kiro --product my-app
```

### 6. Use Topics for Context Switching

**Good:**
```bash
# Create topic for focused work
kob topic create feature-x --agent kiro
kob topic add feature-x --item MYAPP-TSK-0001
kob topic add feature-x --item MYAPP-TSK-0002
kob topic switch feature-x --agent kiro

# All context in one place
```

**Bad:**
```bash
# Jumping between unrelated items without grouping
# Context scattered, hard to maintain focus
```

### 7. Refresh Views Regularly

**Good:**
```bash
# After completing work, refresh views
kob workitem update-state MYAPP-TSK-0001 --state Done --agent kiro --product my-app
kob view refresh --product my-app --agent kiro
```

**Automatic refresh:**
```toml
# In config file: .kano/backlog_config.toml
[views]
auto_refresh = true  # Views refresh after item changes
```

### 8. Clean Up Regularly

**Worksets:**
```bash
# Clean up worksets older than 3 days
kob workset cleanup --ttl-hours 72 --apply
```

**Topics:**
```bash
# Clean up closed topics older than 2 weeks
kob topic cleanup --ttl-days 14 --apply
```

---

## Environment Variables

Common environment variables for configuration:

```bash
# Backlog root override
export KANO_BACKLOG_ROOT=/path/to/backlog

# Default product
export KANO_BACKLOG_DEFAULT_PRODUCT=my-app

# Cache root override
export KANO_CACHE_ROOT=/path/to/cache

# Profile selection
export KANO_BACKLOG_PROFILE=production

# Config file override
export KANO_CONFIG_FILE=/path/to/config.toml

# Agent identity (for scripts)
export KANO_AGENT_ID=kiro
```

**Usage:**
```bash
# Set environment variables
export KANO_BACKLOG_DEFAULT_PRODUCT=my-app
export KANO_AGENT_ID=kiro

# Commands use environment defaults
kob item create --type task --title "..."
# Automatically uses my-app as product and kiro as agent
```

---

## Troubleshooting

### Issue: "Backlog not initialized"

**Error:**
```
Error: Backlog root not found: _kano/backlog/
```

**Solution:**
```bash
kob admin init --product my-app --agent kiro
```

### Issue: "Ready gate validation failed"

**Error:**
```
Error: Cannot transition to Ready state. Missing required fields:
  - Context
  - Approach
```

**Solution:**
```bash
kob workitem set-ready MYAPP-TSK-0001 --product my-app \
  --context "..." --goal "..." --approach "..." \
  --acceptance-criteria "..." --risks "..."
```

### Issue: "Invalid state transition"

**Error:**
```
Error: Invalid state transition: Proposed → Done
Valid transitions from Proposed: Planned, Dropped
```

**Solution:**
```bash
# Follow valid state transitions
kob workitem update-state MYAPP-TSK-0001 --state Planned --agent kiro --product my-app
kob workitem set-ready MYAPP-TSK-0001 --product my-app --context "..." --goal "..." --approach "..." --acceptance-criteria "..." --risks "..."
kob workitem update-state MYAPP-TSK-0001 --state InProgress --agent kiro --product my-app
kob workitem update-state MYAPP-TSK-0001 --state Done --agent kiro --product my-app
```

### Issue: "Item not found"

**Error:**
```
Error: Item not found: MYAPP-TSK-9999
```

**Solution:**
```bash
# List items to find correct ID
kob item list --product my-app

# Or search for it
kob search query "keyword" --product my-app
```

### Issue: "Sequence number conflict"

**Error:**
```
Error: Sequence number conflict for MYAPP-TSK-0042
```

**Solution:**
```bash
# Sync sequences to resolve conflicts
kob admin sync-sequences --product my-app
```

---

## Additional Resources

- **[Quick Start Guide](quick-start.md)** - Get started in 5-10 minutes
- **[Installation Guide](installation.md)** - System requirements and setup
- **[Configuration Guide](configuration.md)** - Profiles and settings
- **[Workset Documentation](workset.md)** - Prevent agent drift
- **[Topic Documentation](topic.md)** - Context switching
- **[SKILL.md](../SKILL.md)** - Complete workflow rules for agents
- **[Schema Reference](../references/schema.md)** - Item types and states
- **[CHANGELOG.md](../CHANGELOG.md)** - Version history

---

**Remember**: The backlog is your institutional memory. Use it to capture decisions, not just track tasks.
