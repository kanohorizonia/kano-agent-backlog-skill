# Testing Guide

Comprehensive testing documentation for kano-agent-backlog-skill.

## Quick Start

### 1. Quick Smoke Test (~30 seconds)

Fast validation without coverage:

```bash
cd skills/kano-agent-backlog-skill
bash scripts/test/quick-test.sh
```

### 2. Full Test Suite with Coverage (~2-5 minutes)

Comprehensive tests with coverage reporting:

```bash
cd skills/kano-agent-backlog-skill
bash scripts/test/run-all-tests.sh
# Coverage report: file://$(pwd)/.coverage_html/index.html
```

### 3. Lint & Type Check (~30 seconds)

Code quality validation:

```bash
cd skills/kano-agent-backlog-skill
bash scripts/test/lint.sh              # Check only
bash scripts/test/lint.sh --fix        # Check + auto-fix
```

## Test Scripts

```
scripts/test/
├── quick-test.sh       # Fast smoke test (no coverage)
├── run-all-tests.sh    # Full pytest + coverage
└── lint.sh            # ruff, black, isort, mypy
```

## Test Types

### Unit Tests (Python)

**Location**: `tests/` directory  
**Framework**: pytest + pytest-cov  
**Coverage**: kano_backlog_core, kano_backlog_ops, kano_backlog_cli

| Tool | Purpose |
|------|---------|
| pytest | Test runner |
| pytest-cov | Coverage measurement |
| hypothesis | Property-based testing |

### Smoke Tests (C++)

**Location**: `src/cpp/build/_intermediate/<preset>/bin/`  
**Run**: Built as part of native build, manual execution

## Running Tests

### Before Commit

```bash
# Fast validation
bash scripts/test/quick-test.sh

# Also lint
bash scripts/test/lint.sh
```

### Before Push / Release

```bash
# Full test suite with coverage
bash scripts/test/run-all-tests.sh

# Code quality
bash scripts/test/lint.sh --fix
```

### CI/CD Integration

#### GitHub Actions

```yaml
name: Test kano-agent-backlog-skill

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Install dependencies
        run: |
          python -m pip install -e "skills/kano-agent-backlog-skill[dev]"

      - name: Quick test
        run: bash skills/kano-agent-backlog-skill/scripts/test/quick-test.sh

      - name: Full test with coverage
        run: bash skills/kano-agent-backlog-skill/scripts/test/run-all-tests.sh

      - name: Lint
        run: bash skills/kano-agent-backlog-skill/scripts/test/lint.sh
```

#### Windows (Git Bash)

```bash
# Quick test
bash skills/kano-agent-backlog-skill/scripts/test/quick-test.sh

# Full test
bash skills/kano-agent-backlog-skill/scripts/test/run-all-tests.sh

# Lint
bash skills/kano-agent-backlog-skill/scripts/test/lint.sh --fix
```

#### Linux/macOS

```bash
# Same as above
bash skills/kano-agent-backlog-skill/scripts/test/quick-test.sh
```

## Coverage

Coverage is measured for three packages:

- `kano_backlog_core` — Domain library (models, state, validation, frontmatter, config, refs)
- `kano_backlog_ops` — Business logic (workitem, view, index, templates, topic, workset, orchestration, doctor)
- `kano_backlog_cli` — CLI entry point (main CLI facade)

**Coverage target**: 80%+ line coverage for core modules.

### Viewing Coverage

After running `run-all-tests.sh`:

```bash
# Open in browser (Windows)
start .coverage_html/index.html

# Linux
xdg-open .coverage_html/index.html

# macOS
open .coverage_html/index.html
```

## Troubleshooting

### pytest not found

```bash
pip install pytest pytest-cov
# or
pip install -e "skills/kano-agent-backlog-skill[dev]"
```

### Coverage tools missing

```bash
pip install pytest-cov
```

### Lint tools missing

```bash
pip install ruff black isort mypy
# or
pip install -e "skills/kano-agent-backlog-skill[dev]"
```

### Tests fail on Windows path issues

The skill uses forward-slash paths internally. If running in Git Bash/MSYS2, paths should work correctly. For Windows CMD/PowerShell, use Git Bash or WSL.

### C++ smoke test binary not found

Build the native binary first:

```bash
# Windows (Git Bash)
bash skills/kano-agent-backlog-skill/scripts/internal/self-build.sh debug

# Linux
bash skills/kano-agent-backlog-skill/scripts/internal/self-build.sh debug

# macOS
bash skills/kano-agent-backlog-skill/scripts/internal/self-build.sh debug
```

## Adding New Tests

### Python Tests

1. Add test file to `tests/` directory:
   ```bash
   touch tests/test_my_feature.py
   ```

2. Follow pytest naming convention:
   - Files: `test_*.py`
   - Classes: `Test*`
   - Functions: `test_*`

3. Example:
   ```python
   def test_my_feature_loads():
       from kano_backlog_core import MyClass
       assert MyClass() is not None
   ```

4. Run to verify:
   ```bash
   python -m pytest tests/test_my_feature.py -v
   ```

### Running a Single Test File

```bash
python -m pytest tests/test_my_feature.py -v
```

### Running Tests Matching a Pattern

```bash
python -m pytest -k "test_name_pattern" -v
```

## Coverage Exclusion

To exclude lines from coverage, use:

```python
if False:  # pragma: no cover
    # unreachable code
```

Or use `# noqa` comments to exclude linting.

## Best Practices

1. **Run quick test before every commit** — fast feedback
2. **Run full test suite before push** — catch regressions
3. **Run lint before code review** — consistent style
4. **Maintain 80%+ coverage** on core modules
5. **Use hypothesis for property-based tests** — find edge cases
6. **Keep tests isolated** — each test is independent
7. **Name tests descriptively** — `test_parses_valid_yaml_frontmatter`
