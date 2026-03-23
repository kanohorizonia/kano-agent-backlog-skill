# Publishing to PyPI

This guide explains how to publish kano-agent-backlog-skill to PyPI (Python Package Index).

## Prerequisites

### 1. Install Publishing Tools

```bash
pip install build twine
```

### 2. Create PyPI Accounts

- **Test PyPI** (for testing): https://test.pypi.org/account/register/
- **Production PyPI**: https://pypi.org/account/register/

### 3. Configure API Tokens

Create API tokens for authentication:

1. Go to Account Settings → API tokens
2. Create a new token with "Upload packages" scope
3. Save the token securely (you won't see it again)

#### Option A: Use ~/.pypirc (Recommended)

Create or edit `~/.pypirc`:

```ini
[distutils]
index-servers =
    pypi
    testpypi

[pypi]
username = __token__
password = pypi-YOUR-PRODUCTION-TOKEN-HERE

[testpypi]
repository = https://test.pypi.org/legacy/
username = __token__
password = pypi-YOUR-TEST-TOKEN-HERE
```

Set proper permissions:
```bash
chmod 600 ~/.pypirc
```

#### Option B: Use Environment Variables

```bash
export TWINE_USERNAME=__token__
export TWINE_PASSWORD=pypi-YOUR-TOKEN-HERE
```

## Publishing Workflow

### Step 1: Prepare for Release

1. **Update version** in `src/kano_backlog_core/__version__.py`:
   ```python
   __version__ = "0.1.0"
   ```

2. **Update CHANGELOG.md** with release notes

3. **Run all checks**:
   ```bash
   # Run tests
   pytest tests/ -v
   
   # Type checking
   mypy src/
   
   # Linting
   black --check src/ tests/
   isort --check src/ tests/
   ```

4. **Commit changes**:
   ```bash
   git add .
   git commit -m "Release 0.1.0"
   ```

### Step 2: Build Distribution Packages

```bash
# Clean previous builds
rm -rf dist/ build/ *.egg-info

# Build source distribution and wheel
python -m build
```

This creates:
- `dist/kano_agent_backlog_skill-0.1.0.tar.gz` (source distribution)
- `dist/kano_agent_backlog_skill-0.1.0-py3-none-any.whl` (wheel)

### Step 3: Verify Distribution

```bash
# Check package metadata and structure
twine check dist/*

# Inspect wheel contents
python -m zipfile -l dist/*.whl
```

### Step 4: Test Installation Locally

```bash
# Create fresh virtual environment
python -m venv test-venv
source test-venv/bin/activate  # or test-venv\Scripts\activate on Windows

# Install from wheel
pip install dist/kano_agent_backlog_skill-0.1.0-py3-none-any.whl

# Verify CLI works
bash scripts/internal/show-version.sh
kob doctor

# Test basic workflow
kob admin init --product test --agent test-agent
kob item create --type task --title "Test" --product test --agent test-agent

# Clean up
deactivate
rm -rf test-venv
```

### Step 5: Upload to Test PyPI

```bash
# Using the script (recommended)
./scripts/publish_to_pypi.sh test

# Or manually
twine upload --repository testpypi dist/*
```

### Step 6: Test Installation from Test PyPI

```bash
# Create fresh virtual environment
python -m venv test-pypi-venv
source test-pypi-venv/bin/activate

# Install from Test PyPI
pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill

# Verify it works
bash scripts/internal/show-version.sh
kob doctor

# Clean up
deactivate
rm -rf test-pypi-venv
```

### Step 7: Upload to Production PyPI

**⚠️ WARNING: This makes the package publicly available to everyone!**

```bash
# Using the script (recommended)
./scripts/publish_to_pypi.sh prod

# Or manually
twine upload dist/*
```

### Step 8: Create Git Tag and GitHub Release

```bash
# Create annotated tag
git tag -a v0.1.0 -m "Release 0.1.0 alpha"

# Push tag to GitHub
git push origin v0.1.0
```

Then create a GitHub Release:
1. Go to https://github.com/yourusername/kano-agent-backlog-skill/releases/new
2. Select the tag `v0.1.0`
3. Add release title: "Release 0.1.0 Alpha"
4. Copy release notes from CHANGELOG.md
5. Attach distribution files from `dist/`
6. Mark as "pre-release" if it's alpha/beta
7. Publish release

### Step 9: Verify Public Installation

```bash
# Create fresh virtual environment
python -m venv verify-venv
source verify-venv/bin/activate

# Install from PyPI
pip install kano-agent-backlog-skill

# Verify
bash scripts/internal/show-version.sh
kob doctor

# Clean up
deactivate
rm -rf verify-venv
```

## Using the Publishing Scripts

### Bash (Linux/macOS)

```bash
# Make script executable
chmod +x scripts/publish_to_pypi.sh

# Upload to Test PyPI
./scripts/publish_to_pypi.sh test

# Upload to Production PyPI
./scripts/publish_to_pypi.sh prod
```

### PowerShell (Windows)

```powershell
# Upload to Test PyPI
.\scripts\publish_to_pypi.ps1 -Environment test

# Upload to Production PyPI
.\scripts\publish_to_pypi.ps1 -Environment prod
```

## Troubleshooting

### "Package already exists" Error

If you try to upload the same version twice:
- PyPI doesn't allow overwriting existing versions
- You must bump the version number and rebuild
- Or delete the version from PyPI (only possible within first few hours)

### Authentication Errors

```
403 Forbidden: Invalid or non-existent authentication information
```

Solutions:
- Verify your API token is correct
- Check `~/.pypirc` permissions (should be 600)
- Ensure username is `__token__` (not your PyPI username)
- Token should start with `pypi-`

### Missing Dependencies in Installed Package

If users report missing dependencies:
- Check `pyproject.toml` [project.dependencies]
- Rebuild and re-upload with correct dependencies
- Bump version number (can't overwrite existing version)

### Wheel Contains Wrong Files

If distribution includes unwanted files:
- Check `.gitignore` and `MANIFEST.in`
- Verify `[tool.setuptools.packages.find]` in `pyproject.toml`
- Clean build artifacts: `rm -rf dist/ build/ *.egg-info`
- Rebuild: `python -m build`

## Best Practices

1. **Always test on Test PyPI first** before uploading to production
2. **Use semantic versioning**: MAJOR.MINOR.PATCH
3. **Update CHANGELOG.md** before every release
4. **Create git tags** for every release
5. **Test installation** in fresh virtual environment
6. **Keep API tokens secure** - never commit them to git
7. **Use API tokens** instead of username/password
8. **Document breaking changes** clearly in CHANGELOG

## Security Notes

- **Never commit** `~/.pypirc` or API tokens to git
- **Use separate tokens** for Test PyPI and Production PyPI
- **Rotate tokens** if they're ever exposed
- **Limit token scope** to "Upload packages" only
- **Use 2FA** on your PyPI account

## References

- [Python Packaging User Guide](https://packaging.python.org/)
- [PyPI Help](https://pypi.org/help/)
- [Twine Documentation](https://twine.readthedocs.io/)
- [Semantic Versioning](https://semver.org/)
