#!/bin/bash
# Publish kano-agent-backlog-skill to PyPI
# 
# Usage:
#   ./scripts/publish_to_pypi.sh [test|prod]
#
# Arguments:
#   test - Upload to test.pypi.org (default)
#   prod - Upload to pypi.org (production)
#
# Prerequisites:
#   - pip install twine
#   - Configure PyPI credentials in ~/.pypirc or use environment variables

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default to test environment
ENVIRONMENT="${1:-test}"

echo "=========================================="
echo "PyPI Publishing Script"
echo "=========================================="
echo ""

# Validate environment argument
if [[ "$ENVIRONMENT" != "test" && "$ENVIRONMENT" != "prod" ]]; then
    echo -e "${RED}Error: Invalid environment '$ENVIRONMENT'${NC}"
    echo "Usage: $0 [test|prod]"
    exit 1
fi

# Change to project root
cd "$PROJECT_ROOT"

# Check if twine is installed
if ! command -v twine &> /dev/null; then
    echo -e "${RED}Error: twine is not installed${NC}"
    echo "Install with: pip install twine"
    exit 1
fi

# Get version from __version__.py
VERSION=$(python -c "import sys; sys.path.insert(0, 'src/python'); from kano_backlog_core.__version__ import __version__; print(__version__)")
echo -e "${GREEN}Package version: $VERSION${NC}"
echo ""

# Check if dist/ directory exists and has files
if [[ ! -d "dist" ]] || [[ -z "$(ls -A dist 2>/dev/null)" ]]; then
    echo -e "${YELLOW}Warning: dist/ directory is empty or doesn't exist${NC}"
    echo "Building package first..."
    echo ""
    
    # Clean previous builds
    rm -rf dist/ build/ *.egg-info
    
    # Build package
    python -m build
    
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Error: Package build failed${NC}"
        exit 1
    fi
    echo ""
fi

# List distribution files
echo "Distribution files:"
ls -lh dist/
echo ""

# Verify distribution files exist
WHEEL_FILE=$(ls dist/*.whl 2>/dev/null | head -n 1)
SDIST_FILE=$(ls dist/*.tar.gz 2>/dev/null | head -n 1)

if [[ -z "$WHEEL_FILE" ]] || [[ -z "$SDIST_FILE" ]]; then
    echo -e "${RED}Error: Missing distribution files${NC}"
    echo "Expected: .whl and .tar.gz files in dist/"
    exit 1
fi

# Check distribution with twine
echo "Checking distribution files..."
twine check dist/*

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Error: Distribution check failed${NC}"
    exit 1
fi
echo ""

# Confirm upload
if [[ "$ENVIRONMENT" == "prod" ]]; then
    echo -e "${YELLOW}=========================================="
    echo "WARNING: You are about to upload to PRODUCTION PyPI"
    echo "==========================================${NC}"
    echo ""
    echo "Package: kano-agent-backlog-skill"
    echo "Version: $VERSION"
    echo "Target: https://pypi.org"
    echo ""
    read -p "Are you sure you want to continue? (yes/no): " CONFIRM
    
    if [[ "$CONFIRM" != "yes" ]]; then
        echo "Upload cancelled."
        exit 0
    fi
else
    echo -e "${GREEN}Uploading to TEST PyPI${NC}"
    echo "Package: kano-agent-backlog-skill"
    echo "Version: $VERSION"
    echo "Target: https://test.pypi.org"
    echo ""
fi

# Upload to PyPI
if [[ "$ENVIRONMENT" == "prod" ]]; then
    echo "Uploading to PyPI..."
    twine upload dist/*
    
    if [[ $? -eq 0 ]]; then
        echo ""
        echo -e "${GREEN}=========================================="
        echo "Successfully uploaded to PyPI!"
        echo "==========================================${NC}"
        echo ""
        echo "Package URL: https://pypi.org/project/kano-agent-backlog-skill/$VERSION/"
        echo ""
        echo "Users can now install with:"
        echo "  pip install kano-agent-backlog-skill"
        echo ""
        echo "Next steps:"
        echo "  1. Create git tag: git tag -a v$VERSION -m 'Release $VERSION'"
        echo "  2. Push tag: git push origin v$VERSION"
        echo "  3. Create GitHub release with release notes"
        echo "  4. Verify installation: pip install kano-agent-backlog-skill==$VERSION"
    else
        echo -e "${RED}Error: Upload to PyPI failed${NC}"
        exit 1
    fi
else
    echo "Uploading to Test PyPI..."
    twine upload --repository testpypi dist/*
    
    if [[ $? -eq 0 ]]; then
        echo ""
        echo -e "${GREEN}=========================================="
        echo "Successfully uploaded to Test PyPI!"
        echo "==========================================${NC}"
        echo ""
        echo "Package URL: https://test.pypi.org/project/kano-agent-backlog-skill/$VERSION/"
        echo ""
        echo "Test installation with:"
        echo "  pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill"
        echo ""
        echo "If everything works, upload to production with:"
        echo "  $0 prod"
    else
        echo -e "${RED}Error: Upload to Test PyPI failed${NC}"
        exit 1
    fi
fi
