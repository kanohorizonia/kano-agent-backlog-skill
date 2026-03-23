# Setup Summary for kano-agent-backlog-skill

Quick reference for different installation scenarios.

## For End Users (PyPI Installation)

**When available on PyPI:**

```bash
# Install from PyPI
pip install kano-agent-backlog-skill

# Verify
bash scripts/internal/show-version.sh
kob
kob doctor

# Initialize backlog
cd /path/to/your/project
kob admin init --product my-app --agent <your-agent>

# Add cache and logs to .gitignore (IMPORTANT)
echo ".kano/cache" >> .gitignore
echo "_kano/backlog/_shared/logs" >> .gitignore
```

**Status:** Not yet published to PyPI (alpha release)

See: [Quick Start Guide](quick-start.md)

## For Developers (Cloned Repository)

**When working with cloned source code:**

```bash
# Navigate to skill directory
cd skills/kano-agent-backlog-skill

# Install in editable mode with dev dependencies
pip install -e ".[dev]"

# Verify installation
bash scripts/internal/show-version.sh
kob
kob doctor

# Initialize backlog in your project
cd /path/to/your/project
kob admin init --product my-app --agent <your-agent>

# Add cache and logs to .gitignore (IMPORTANT)
echo ".kano/cache" >> .gitignore
echo "_kano/backlog/_shared/logs" >> .gitignore
```

**Key difference:** `-e` flag installs in "editable mode" so code changes take effect immediately.

See: [Agent Quick Start Guide](agent-quick-start.md)

## For AI Agents

**When helping users set up from cloned repo:**

1. **Check prerequisites:**
   ```bash
   python --version  # Must be 3.8+
   which python      # Should be in venv
   ```

2. **Install in editable mode:**
   ```bash
   cd skills/kano-agent-backlog-skill
   pip install -e ".[dev]"
   ```

3. **Verify:**
   ```bash
   bash scripts/internal/show-version.sh
   kob
   kob doctor
   ```

4. **Initialize:**
   ```bash
   cd /path/to/project
   kob admin init --product <product> --agent <agent-id>
   
   # Add cache and logs to .gitignore (IMPORTANT)
   echo ".kano/cache" >> .gitignore
   echo "_kano/backlog/_shared/logs" >> .gitignore
   ```

**Important:** Always use explicit `--agent` flags (e.g., `kiro`, `copilot`, `claude`), never placeholders.

See: [Agent Quick Start Guide](agent-quick-start.md)

## For Contributors

**When contributing to the skill:**

```bash
# Clone repository
git clone https://github.com/yourusername/kano-agent-backlog-skill.git
cd kano-agent-backlog-skill

# Create virtual environment
python -m venv .venv
source .venv/bin/activate  # or .venv\Scripts\activate on Windows

# Install with dev dependencies
pip install -e ".[dev]"

# Run tests
pytest tests/

# Run type checking
mypy src/

# Format code
black src/ tests/
isort src/ tests/
```

See: [CONTRIBUTING.md](../CONTRIBUTING.md)

## Publishing to PyPI (Maintainers Only)

**When ready to publish a release:**

```bash
# Test on Test PyPI first
./scripts/publish_to_pypi.sh test

# Verify test installation
pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill

# If all good, publish to production
./scripts/publish_to_pypi.sh prod
```

See: [Publishing to PyPI Guide](publishing-to-pypi.md)

## Quick Command Reference

### Installation Verification
```bash
bash scripts/internal/show-version.sh    # Check version
kob                              # Inspect command surface
kob doctor                       # Validate environment
```

### Backlog Initialization
```bash
kob admin init --product <name> --agent <agent>

# IMPORTANT: Add cache and logs to .gitignore after initialization
echo ".kano/cache" >> .gitignore
echo "_kano/backlog/_shared/logs" >> .gitignore
```

### Common Operations
```bash
# Create item
kob item create --type task --title "<title>" --product <product> --agent <agent>

# List items
kob item list --product <product>

# Update state
kob workitem update-state <ID> --state <state> --product <product>

# Create ADR
kob adr create --title "<title>" --product <product> --agent <agent>
```

## Documentation Index

- **[Agent Quick Start](agent-quick-start.md)** - For AI agents setting up from cloned repo
- **[Quick Start Guide](quick-start.md)** - For end users installing from PyPI
- **[Installation Guide](installation.md)** - Detailed setup and troubleshooting
- **[Configuration Guide](configuration.md)** - Advanced configuration
- **[Publishing to PyPI](publishing-to-pypi.md)** - Release process for maintainers
- **[CONTRIBUTING.md](../CONTRIBUTING.md)** - Development guidelines
- **[SKILL.md](../SKILL.md)** - Complete workflow rules for agents

## Current Status

- **Version:** 0.1.0
- **Status:** Alpha (not yet on PyPI)
- **Installation:** Development mode only (`pip install -e .`)
- **Stability:** API may change significantly

---

**Choose your path:**
- 🤖 AI Agent? → [Agent Quick Start](agent-quick-start.md)
- 👤 End User? → [Quick Start Guide](quick-start.md)
- 👨‍💻 Developer? → [CONTRIBUTING.md](../CONTRIBUTING.md)
- 📦 Maintainer? → [Publishing to PyPI](publishing-to-pypi.md)
