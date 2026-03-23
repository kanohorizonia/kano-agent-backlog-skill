# Contributing to kano-agent-backlog-skill

Thank you for your interest in contributing to kano-agent-backlog-skill! This document provides guidelines and instructions for developers who want to contribute to the project.

## Table of Contents

- [Development Setup](#development-setup)
- [Code Style Guidelines](#code-style-guidelines)
- [Testing](#testing)
- [Commit Convention](#commit-convention)
- [Backlog Discipline](#backlog-discipline)
- [Building and Testing Locally](#building-and-testing-locally)
- [Release Process](#release-process)

## Development Setup

### Prerequisites

- Python 3.8 or higher
- Git
- Virtual environment tool (venv, virtualenv, or conda)

### Initial Setup

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/kano-agent-backlog-skill.git
   cd kano-agent-backlog-skill
   ```

2. **Create and activate a virtual environment**:
   ```bash
   python -m venv .venv
   
   # On Unix/macOS:
   source .venv/bin/activate
   
   # On Windows:
   .venv\Scripts\activate
   ```

3. **Install the package in editable mode with development dependencies**:
   ```bash
   python -m pip install -U pip
   python -m pip install -e .[dev]
   ```

4. **Verify installation**:
   ```bash
   bash scripts/internal/show-version.sh
   kob
   kob doctor
   ```

## Code Style Guidelines

We follow strict code style guidelines to maintain consistency across the codebase.

### Formatting Tools

- **Black**: Code formatter with line length 88
- **isort**: Import sorter with Black-compatible profile
- **mypy**: Static type checker in strict mode

### Running Code Style Checks

```bash
# Format code
black src/ tests/

# Sort imports
isort src/ tests/

# Type checking
mypy src/

# Run all checks together
black src/ tests/ && isort src/ tests/ && mypy src/
```

### Type Hints

Always use type hints from the `typing` module:

```python
from typing import List, Dict, Optional, Any

def process_items(items: List[str]) -> Dict[str, Any]:
    """Process items and return a dictionary."""
    result: Dict[str, Any] = {}
    for item in items:
        result[item] = len(item)
    return result
```

### Naming Conventions

- **Classes**: `PascalCase` (e.g., `BacklogItem`, `ConfigLoader`)
- **Functions**: `snake_case` (e.g., `read_item`, `validate_config`)
- **Constants**: `UPPER_SNAKE_CASE` (e.g., `DEFAULT_TIMEOUT`)
- **Private members**: Leading underscore `_private_var`
- **Modules**: `snake_case` (e.g., `kano_backlog_core`)

### Docstrings

Use Google-style docstrings:

```python
def create_item(title: str, item_type: str) -> BacklogItem:
    """Create a new backlog item.

    Args:
        title: The title of the item.
        item_type: The type of item (EPIC, FEATURE, USER_STORY, TASK, BUG).

    Returns:
        The created BacklogItem object.

    Raises:
        ValidationError: If the item type is invalid.
    """
    # Implementation here
```

## Testing

We use both unit tests and property-based tests for comprehensive coverage.

### Running Tests

```bash
# Run all tests
pytest tests/ -v

# Run specific test file
pytest tests/test_doctor.py -v

# Run with coverage
pytest tests/ --cov=src --cov-report=html

# Run property-based tests with full iterations
pytest tests/ -v -k property
```

### Writing Tests

#### Unit Tests

Focus on specific examples, edge cases, and error conditions:

```python
def test_python_version_meets_requirements():
    """Test that current Python version passes the check."""
    result = check_python_version()
    
    assert result.name == "Python Version"
    assert result.passed is True
    assert "meets requirements" in result.message
```

#### Property-Based Tests

Use Hypothesis for property-based testing with minimum 100 iterations:

```python
from hypothesis import given, strategies as st

@given(st.lists(st.text(min_size=1, max_size=50), min_size=1, max_size=100))
def test_id_assignment_uniqueness(item_titles):
    """Feature: release-0-1-0-beta, Property 5: ID Assignment Uniqueness"""
    # Test implementation
    assert len(assigned_ids) == len(set(assigned_ids))
```

### Test Organization

- Place tests in `tests/` directory
- Test filename: `test_{module_name}.py`
- Use descriptive test names that explain what is being tested
- Group related tests in classes

## Commit Convention

### Commit Message Format

Reference backlog IDs directly in commit messages:

```
KABSD-TSK-0146: Add Python version check to doctor command

KABSD-TSK-0147 KABSD-TSK-0148: Implement SQLite and permissions checks
```

**Important**: Do NOT use `jira#` prefix. This repository uses kano-backlog as the system of record.

### Good Examples

- ✅ `KABSD-TSK-0261: Refine filename truncation`
- ✅ `KABSD-FTR-0042: Add vector search support`
- ✅ `KABSD-BUG-0015: Fix state transition validation`

### Bad Examples

- ❌ `jira#KABSD-TSK-0261: ...`
- ❌ `Fix bug` (no backlog reference)
- ❌ `WIP` (not descriptive)

## Backlog Discipline

This project uses kano-agent-backlog-skill for planning and tracking work.

### Before Making Changes

1. **Create or update backlog items** before writing code:
   ```bash
   kob item create \
     --type task \
     --title "Implement X feature" \
     --product kano-agent-backlog-skill \
     --agent <your-agent-id>
   ```

2. **Enforce the Ready gate** on Task/Bug items:
   - Required fields: Context, Goal, Approach, Acceptance Criteria, Risks/Dependencies
   - All fields must be non-empty and written in English

3. **Update item state** when starting work:
   ```bash
    kob workitem update-state \
      KABSD-TSK-0146 \
      --state InProgress \
      --product kano-agent-backlog-skill
   ```

### Worklog Discipline

Worklog is append-only. Append when:
- A load-bearing decision is made
- An item state changes
- Scope/approach changes
- An ADR is created/linked

Format:
```
YYYY-MM-DD HH:MM [agent=<agent-id>] [model=<model>] description
```

**Always provide explicit `--agent <id>`** - never use placeholders.

### Valid Agent IDs

- `copilot`, `codex`, `claude`, `goose`, `antigravity`, `cursor`, `windsurf`, `opencode`, `kiro`, `amazon-q`

## Building and Testing Locally

### Building the Package

```bash
# Clean previous builds
rm -rf dist/ build/ *.egg-info

# Build source distribution and wheel
python -m build

# Verify artifacts
ls -lh dist/
```

### Testing Installation

```bash
# Create fresh virtual environment
python -m venv test-venv
source test-venv/bin/activate  # or test-venv\Scripts\activate on Windows

# Install from wheel
pip install dist/kano_agent_backlog_skill-*.whl

# Verify CLI is available
which kob
bash scripts/internal/show-version.sh
kob doctor

# Test basic workflow
kob admin init --product test-product --agent test-agent
kob item create --type task --title "Test task" --product test-product --agent test-agent
kob item list --product test-product

# Clean up
deactivate
rm -rf test-venv
```

### Pre-commit Checks

Before committing, ensure:

```bash
# Format and lint
black src/ tests/
isort src/ tests/
mypy src/

# Run tests
pytest tests/ -v

# Build package
python -m build
```

## Release Process

For maintainers preparing a release, see the detailed [Release Checklist](docs/release-checklist.md).

### Version Bumping

1. Update version in `src/kano_backlog_core/__version__.py`:
   ```python
   __version__ = "0.2.0"
   ```

2. Follow [Semantic Versioning](https://semver.org/):
   - **MAJOR**: Breaking changes
   - **MINOR**: New features (backward compatible)
   - **PATCH**: Bug fixes (backward compatible)
   - **0.x.y**: Pre-1.0 releases (breaking changes allowed)

### Changelog Maintenance

Update `CHANGELOG.md` following [Keep a Changelog](https://keepachangelog.com/) format:

```markdown
## [0.2.0] - 2024-XX-XX

### Added
- New feature X
- New command Y

### Changed
- Improved Z

### Fixed
- Bug fix for W
```

### Git Tagging Convention

Create annotated tags with format `v{version}`:

```bash
git tag -a v0.2.0 -m "Release 0.2.0: Add feature X"
git push origin v0.2.0
```

## Getting Help

- **Issues**: Open an issue on GitHub for bugs or feature requests
- **Discussions**: Use GitHub Discussions for questions and ideas
- **Documentation**: Check the [docs/](docs/) directory for detailed guides

## Code of Conduct

Be respectful, constructive, and collaborative. We're all here to build something useful together.

## License

By contributing to kano-agent-backlog-skill, you agree that your contributions will be licensed under the MIT License.
