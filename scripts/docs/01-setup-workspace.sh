#!/bin/bash
set -euo pipefail

# Local workspace setup script - mimics GitHub Actions checkout structure
# Run this to set up _ws/ directory for testing docs-prepare-quartz.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# Load configuration
CONFIG_FILE="$SCRIPT_DIR/config/build.json"
if [ ! -f "$CONFIG_FILE" ]; then
  echo "Error: Configuration file not found: $CONFIG_FILE"
  exit 1
fi

# Extract configuration values using basic JSON parsing
QUARTZ_VERSION=$(grep -o '"version"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | cut -d'"' -f4)
QUARTZ_REPO=$(grep -o '"repository"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | head -1 | cut -d'"' -f4)
SKILL_REPO=$(grep -A5 '"repositories"' "$CONFIG_FILE" | grep -o '"skill"[[:space:]]*:[[:space:]]*"[^"]*"' | cut -d'"' -f4)

echo "Setting up workspace structure in: $REPO_ROOT"
echo "Using Quartz version: $QUARTZ_VERSION"

# Clean existing workspace
if [ -d "_ws" ]; then
  echo "Updating existing workspace..."
  
  # Update demo repo (current repo)
  if [ -d "_ws/src/demo" ]; then
    echo "Updating demo repo..."
    cd _ws/src/demo
    git fetch origin
    git reset --hard origin/$(git rev-parse --abbrev-ref HEAD)
    cd "$REPO_ROOT"
  else
    echo "Cloning demo repo (current repo)..."
    git clone . _ws/src/demo
  fi
  
  # Update Quartz repo
  if [ -d "_ws/src/quartz" ]; then
    echo "Updating Quartz repo..."
    cd _ws/src/quartz
    git fetch origin
    git checkout "$QUARTZ_VERSION"
    cd "$REPO_ROOT"
  else
    echo "Cloning Quartz repo..."
    git clone --branch "$QUARTZ_VERSION" --depth 1 "$QUARTZ_REPO" _ws/src/quartz
  fi
  
  # Update skill repo
  if [ -d "_ws/src/skill" ]; then
    echo "Updating skill repo..."
    cd _ws/src/skill
    git fetch origin
    git reset --hard origin/$(git rev-parse --abbrev-ref HEAD)
    cd "$REPO_ROOT"
  else
    echo "Cloning skill repo..."
    git clone "$SKILL_REPO" _ws/src/skill
  fi
else
  echo "Creating new workspace..."
  mkdir -p _ws
  
  echo "Cloning demo repo (current repo)..."
  git clone . _ws/src/demo
  
  echo "Cloning Quartz repo..."
  git clone --branch "$QUARTZ_VERSION" --depth 1 "$QUARTZ_REPO" _ws/src/quartz
  
  echo "Cloning skill repo..."
  git clone "$SKILL_REPO" _ws/src/skill
fi

# Create build and deploy directories
mkdir -p _ws/build/{content,public}
mkdir -p _ws/deploy

echo ""
echo "Workspace setup complete!"
echo "Structure:"
echo "  _ws/src/demo/     - Demo repo (current repo clone)"
echo "  _ws/src/quartz/   - Quartz $QUARTZ_VERSION engine"
echo "  _ws/src/skill/    - Skill repo clone"
echo "  _ws/build/        - Generated artifacts"
echo "  _ws/deploy/       - Deployment targets"
echo ""
echo "Now you can run:"
echo "  ./scripts/docs/02-prepare-content.sh"
echo ""
echo "To clean up:"
echo "  rm -rf _ws"