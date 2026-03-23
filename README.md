# kano-agent-backlog-skill

[![PyPI version](https://img.shields.io/pypi/v/kano-agent-backlog-skill.svg)](https://pypi.org/project/kano-agent-backlog-skill/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Python 3.8+](https://img.shields.io/badge/python-3.8%2B-blue.svg)](https://www.python.org/downloads/)
[![AI Agent Skills](https://img.shields.io/badge/AI-Agent%20Skills-brightgreen.svg)](https://github.com/topics/ai-agent)
[![Spec-Driven](https://img.shields.io/badge/Spec--Driven-Agentic%20Programming-orange.svg)](https://github.com/topics/agentic-programming)

> **Local-first backlog management for AI agent collaboration** | Turn ephemeral chat context into durable engineering assets

**Transform agent conversations into auditable work items, decisions, and acceptance criteria.**  
Stop losing context when your AI coding assistant forgets what you discussed. Keep a durable trail of *what to build*, *why*, and *how to verify* — all stored as human-readable markdown files in your repository.

> Code can be rewritten. Lost decisions can't.

## Why kano-agent-backlog-skill?

Working with AI coding agents is powerful, but context evaporates:
- **Lost decisions**: "Why didn't we use approach X?" — buried in chat history
- **Missing rationale**: Code works today, but maintenance feels like archaeology
- **Requirement drift**: Changes force you to dig through conversations to understand impact
- **Agent amnesia**: Each new session starts from scratch, losing accumulated context

**kano-agent-backlog-skill** solves this by making your agent work *tickets-first*:
- Create work items (Epic/Feature/Task/Bug) **before** writing code
- Capture decisions in append-only worklogs and Architecture Decision Records (ADRs)
- Enforce a "Ready gate" so every task has Context, Goal, Approach, Acceptance Criteria, and Risks
- Store everything as markdown files — searchable, linkable, version-controlled

**Result**: Your agent becomes a teammate with institutional memory, not just a code generator.

## Key Features

- **📝 Structured Work Items**: Hierarchical backlog (Epic → Feature → User Story → Task/Bug) with frontmatter metadata
- **🔒 Ready Gate Validation**: Enforce required fields (Context, Goal, Approach, Acceptance Criteria, Risks) before work begins
- **📜 Append-Only Worklog**: Auditable decision trail with timestamps and agent identity — never lose context
- **🏗️ Architecture Decision Records (ADRs)**: Capture significant technical decisions with trade-offs and consequences
- **🎯 Worksets**: Per-item execution cache with plan checklists to prevent agent drift during complex tasks
- **🔄 Topics**: Context switching and grouping for rapid focus area changes with code snippet collection
- **👥 Multi-Agent Coordination**: Multiple agents can collaborate on the same backlog with clear handoffs
- **📊 Multiple Views**: Generate Obsidian Dataview dashboards and plain markdown reports
- **🔍 Optional Search**: SQLite indexing and vector embeddings for semantic search (experimental)
- **🌐 Local-First**: All data stored as markdown files — no server required, works offline

## Quick Install

```bash
pip install kano-agent-backlog-skill
```

**Verify installation:**
```bash
bash scripts/internal/show-version.sh
kob
kob doctor
```

## Minimal Usage Example

```bash
# Initialize a backlog for your product
kob admin init --product my-app --agent kiro

# IMPORTANT: Add cache and logs to .gitignore
echo ".kano/cache" >> .gitignore
echo "_kano/backlog/_shared/logs" >> .gitignore

# Create a task with required fields
kob item create --type task \
  --title "Add user authentication" \
  --agent kiro --product my-app

# Set task to Ready state (enforces required fields)
kob workitem set-ready MYAPP-TSK-0001 --product my-app \
  --context "Users need secure login" \
  --goal "Implement JWT-based authentication" \
  --approach "Use bcrypt for passwords, JWT for sessions" \
  --acceptance-criteria "Users can login, logout, and stay authenticated" \
  --risks "Token expiration handling needs testing"

# Start work with a workset to prevent drift
kob workset init --item MYAPP-TSK-0001 --agent kiro
kob workset next --item MYAPP-TSK-0001

# Update state when done
kob workitem update-state MYAPP-TSK-0001 --state Done \
  --agent kiro --product my-app
```

**Note:** Always add `.kano/cache` and `_kano/backlog/_shared/logs` to your `.gitignore` after initialization to avoid committing derived data (embeddings, SQLite indexes, audit logs).

## Documentation

- **[Agent Quick Start](docs/agent-quick-start.md)** - For AI agents: Install from cloned repo and initialize backlog
- **[Quick Start Guide](docs/quick-start.md)** - Get started in 5-10 minutes
- **[Installation Guide](docs/installation.md)** - System requirements and setup
- **[Configuration Guide](docs/configuration.md)** - Profiles, environment variables, and settings
- **[SKILL.md](SKILL.md)** - Complete workflow and rules for agents
- **[Workset Documentation](docs/workset.md)** - Prevent agent drift during tasks
- **[Topic Documentation](docs/topic.md)** - Context switching and grouping
- **[Schema Reference](references/schema.md)** - Item types, states, and frontmatter
- **[Workflow Guide](references/workflow.md)** - When to create items and record decisions
- **[CHANGELOG.md](CHANGELOG.md)** - Version history and release notes

## System Requirements

- **Python**: 3.8 or higher
- **Operating System**: Linux, macOS, or Windows
- **SQLite**: 3.8+ (usually included with Python)
- **Optional**: Obsidian (for Dataview dashboards)

See the [Installation Guide](docs/installation.md) for detailed setup instructions and troubleshooting.

## How It Works

`kano-agent-backlog-skill` guides AI agents into a **tickets-first workflow**:

1. **Plan Before Code**: Create work items (Epic/Feature/Task/Bug) before implementation
2. **Capture Decisions**: Record rationale in append-only worklogs and ADRs
3. **Enforce Quality**: Ready gate ensures every task has minimum required context
4. **Prevent Drift**: Worksets keep agents focused with explicit plan checklists
5. **Enable Handoffs**: Any agent (or human) can pick up where another left off

Everything is stored as **markdown files** in your repository — human-readable, version-controlled, and searchable.

## Core Concepts

### Work Items
Hierarchical structure for organizing work:
- **Epic**: Large initiative spanning multiple features
- **Feature**: Cohesive capability or user-facing functionality  
- **User Story**: Specific user need or scenario
- **Task**: Technical implementation work
- **Bug**: Defect to be fixed

### Ready Gate
Before a Task or Bug can move to "Ready" state, it must have:
- **Context**: Why this work is needed
- **Goal**: What success looks like
- **Approach**: How it will be implemented
- **Acceptance Criteria**: How to verify it works
- **Risks/Dependencies**: What could go wrong or block progress

### Worksets
Per-item execution cache that prevents agent drift:
- **plan.md**: Checklist derived from acceptance criteria
- **notes.md**: Working notes with decision markers
- **deliverables/**: Staging area for work artifacts
- Keeps agents focused on one task at a time

### Topics
Context grouping for rapid focus area changes:
- Group related items and documents
- Collect code snippets and references
- Generate distilled briefs from materials
- Enable quick context switching between work areas

### Architecture Decision Records (ADRs)
Capture significant technical decisions:
- Document the context, decision, and consequences
- Link to related work items
- Preserve rationale for future reference
- Prevent re-litigating settled questions

## CLI Commands

The `kob` CLI provides comprehensive commands for backlog management:

- **`admin`** - Backlog bootstrap and maintenance (init, adr, index, schema, release)
- **`item` / `workitem`** - Create and manage work items (Epic/Feature/UserStory/Task/Bug)
- **`worklog`** - Append worklog entries
- **`workset`** - Per-item execution cache (init/refresh/next/promote/cleanup/detect-adr)
- **`topic`** - Context grouping (templates, snapshots, merge/split, cross-references, switch/export)
- **`view`** - Generate dashboards and reports
- **`snapshot`** - Evidence snapshots (read-only capture)
- **`config`** - Inspect and validate layered configuration
- **`embedding`** - Embedding pipeline operations (build/query/status)
- **`search`** - Vector similarity search
- **`tokenizer`** - Tokenizer adapter configuration and testing
- **`benchmark`** - Deterministic benchmark harness
- **`changelog`** - Generate changelog from backlog
- **`doctor`** - Environment and backlog health checks

Run `kob` or the thin repo wrappers under `scripts/core/` for current local usage guidance.

## Workset Usage

Worksets prevent agent drift during task execution by providing a structured working context:

```bash
# Initialize workset for a task
kob workset init --item TASK-0042 --agent kiro

# Get next action from plan
kob workset next --item TASK-0042

# Detect decisions that should become ADRs
kob workset detect-adr --item TASK-0042

# Promote deliverables to canonical artifacts
kob workset promote --item TASK-0042 --agent kiro

# Clean up expired worksets
kob workset cleanup --ttl-hours 72
```

See [docs/workset.md](docs/workset.md) for complete documentation.

## Topic Usage

Topics enable rapid context switching when focus areas change:

```bash
# Create a topic for related work
kob topic create auth-refactor --agent kiro

# Add items to the topic
kob topic add auth-refactor --item TASK-0042
kob topic add auth-refactor --item BUG-0012

# Pin relevant documents
kob topic pin auth-refactor --doc _kano/backlog/decisions/ADR-0015.md

# Collect a code snippet reference
kob topic add-snippet auth-refactor --file src/auth.py --start 10 --end 25 --agent kiro

# Distill deterministic brief from materials
kob topic distill auth-refactor

# Switch active topic
kob topic switch auth-refactor --agent kiro

# Export context bundle
kob topic export-context auth-refactor --format json

# Close and cleanup
kob topic close auth-refactor --agent kiro
kob topic cleanup --ttl-days 14 --apply
```

See [docs/topic.md](docs/topic.md) for complete documentation.

## Cache Configuration

The backlog skill stores cache files (chunks databases and vector embeddings) in `.kano/cache/backlog/` by default. You can override this location for team collaboration scenarios.

### Configuration Priority

1. **CLI parameter**: `--cache-root /path/to/cache` (highest priority)
2. **Config file**: `config.cache.root = "/path/to/cache"`
3. **Default**: `<repo_root>/.kano/cache/backlog/`

### Using Config File

Add to your product config (`_kano/backlog/products/<product>/_config/config.toml`):

```toml
[cache]
root = "/mnt/nas/shared-cache/backlog"
```

### Using CLI Override

```bash
kob embedding build --product my-product --cache-root /mnt/nas/cache
kob search query "authentication" --product my-product --cache-root /mnt/nas/cache
```

## The Dual-Readability Principle

Topics and Snapshots solve two problems simultaneously:

1. **Human Overload**: Humans need high-level summaries (`brief.md`, Reports) to make decisions without reading all code
2. **Agent Coordination**: Agents need explicit structure (file paths, line numbers, stub inventories) to act without hallucinating

By enforcing **Dual-Readability** (Markdown for humans, JSON/Structured data for Agents), we create a shared workspace where:
- Humans provide direction (via Briefs)
- Agents provide evidence (via Snapshots)
- Both can understand the other's output without translation

## Development Setup

For development or contributing:

```bash
# Clone the repository
git clone https://github.com/yourusername/kano-agent-backlog-skill.git
cd kano-agent-backlog-skill

# Install in editable mode with dev dependencies
python -m pip install -e ".[dev]"

# Run tests
pytest tests/

# Run type checking
mypy src/

# Format code
black src/ tests/
isort src/ tests/
```

## Contributing

PRs welcome, with one rule: **don't turn this into another Jira.**  
The point is to preserve decisions and acceptance, not to worship process.

See [CONTRIBUTING.md](CONTRIBUTING.md) for development guidelines and release process.

## External References

- [Agent Skills Overview (Anthropic/Claude)](https://platform.claude.com/docs/en/agents-and-tools/agent-skills/overview)
- [Versioning Policy](VERSIONING.md)
- [Release Notes](CHANGELOG.md)

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Support

- **Issues**: [GitHub Issues](https://github.com/yourusername/kano-agent-backlog-skill/issues)
- **Discussions**: [GitHub Discussions](https://github.com/yourusername/kano-agent-backlog-skill/discussions)
- **Documentation**: [docs/](docs/)

---

**Remember**: Code can be rewritten. Lost decisions can't.
