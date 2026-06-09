#!/bin/bash
set -euo pipefail

# Local workspace setup script - mimics GitHub Actions checkout structure
# Run this to set up _ws/ directory for testing docs-prepare-quartz.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
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
DEMO_REPO=$(grep -A6 '"repositories"' "$CONFIG_FILE" | grep -o '"demo"[[:space:]]*:[[:space:]]*"[^"]*"' | cut -d'"' -f4)

sync_local_skill_repo() {
  local target_dir="$1"

  if [ -d "$target_dir" ]; then
    mv "$target_dir" "${target_dir}.stale.$$"
  fi
  mkdir -p "$target_dir"

  echo "Syncing local skill repo working tree..."
  local tar_args=()
  if tar --warning=no-file-changed --version >/dev/null 2>&1; then
    tar_args+=(--warning=no-file-changed)
  fi

  tar "${tar_args[@]}" \
    --exclude='./.git' \
    --exclude='./.pixi' \
    --exclude='./_ws' \
    --exclude='./dist-test' \
    --exclude='./build' \
    --exclude='./src/cpp/out' \
    --exclude='./src/cpp/**/out' \
    -cf - . | tar -xf - -C "$target_dir"
}

echo "Setting up workspace structure in: $REPO_ROOT"
echo "Using Quartz version: $QUARTZ_VERSION"

# Clean existing workspace
if [ -d "_ws" ]; then
  echo "Updating existing workspace..."
  
  # Update demo repo
  if [ -d "_ws/src/demo" ]; then
    echo "Updating demo repo..."
    cd _ws/src/demo
    git fetch origin
    git reset --hard origin/$(git rev-parse --abbrev-ref HEAD)
    cd "$REPO_ROOT"
  else
    echo "Cloning demo repo..."
    git clone "$DEMO_REPO" _ws/src/demo
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
  
  # Refresh skill repo from the local working tree
  sync_local_skill_repo "_ws/src/skill"
else
  echo "Creating new workspace..."
  mkdir -p _ws
  
  echo "Cloning demo repo..."
  git clone "$DEMO_REPO" _ws/src/demo
  
  echo "Cloning Quartz repo..."
  git clone --branch "$QUARTZ_VERSION" --depth 1 "$QUARTZ_REPO" _ws/src/quartz
  
  echo "Preparing skill repo working tree snapshot..."
  sync_local_skill_repo "_ws/src/skill"
fi

# Create build and deploy directories
mkdir -p _ws/build/{content,public}
mkdir -p _ws/deploy

echo ""
echo "Workspace setup complete!"
echo "Structure:"
echo "  _ws/src/demo/     - Demo repo clone"
echo "  _ws/src/quartz/   - Quartz $QUARTZ_VERSION engine"
echo "  _ws/src/skill/    - Skill repo working tree snapshot"
echo "  _ws/build/        - Generated artifacts"
echo "  _ws/deploy/       - Deployment targets"
echo ""
echo "Now you can run:"
echo "  ./scripts/docs/02-prepare-content.sh"
echo ""
echo "To clean up:"
echo "  rm -rf _ws"
