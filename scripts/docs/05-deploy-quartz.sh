#!/bin/bash
set -euo pipefail

# Deploy built site to skill repo gh-pages branch
# Run after build-quartz-site.sh to deploy the documentation
#
# Usage: 
#   04-deploy-local.sh [BUILD_DIR] [DEPLOY_DIR] [COMMIT_MESSAGE]
#   If no arguments provided, auto-detect paths for local usage

# Parse arguments or auto-detect paths
if [ $# -ge 2 ]; then
  # Parameterized mode: use provided paths
  BUILD_DIR="$1"
  DEPLOY_DIR="$2"
  COMMIT_MESSAGE="${3:-Deploy docs site from local build}"
  echo "Using provided paths"
else
  # Local mode: auto-detect repository root
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
  BUILD_DIR="$REPO_ROOT/_ws/build"
  DEPLOY_DIR="$REPO_ROOT/_ws/deploy/gh-pages"
  COMMIT_MESSAGE="Deploy docs site from local build"
  echo "Auto-detected paths"
fi

# Validate workspace structure
if [ ! -d "$BUILD_DIR/staged" ]; then
  echo "Error: Build staged directory not found: $BUILD_DIR/staged"
  exit 1
fi

# Clone skill repo gh-pages branch if not exists
if [ ! -d "$DEPLOY_DIR" ]; then
  echo "Setting up gh-pages branch..."
  mkdir -p "$(dirname "$DEPLOY_DIR")"
  
  # Try to clone existing gh-pages branch, if it doesn't exist, create it
  if git ls-remote --heads https://github.com/dorgonman/kano-agent-backlog-skill.git gh-pages | grep -q gh-pages; then
    echo "Cloning existing gh-pages branch..."
    git clone --branch gh-pages https://github.com/dorgonman/kano-agent-backlog-skill.git "$DEPLOY_DIR"
  else
    echo "Creating new gh-pages branch..."
    git clone https://github.com/dorgonman/kano-agent-backlog-skill.git "$DEPLOY_DIR"
    cd "$DEPLOY_DIR"
    git checkout --orphan gh-pages
    git rm -rf .
    echo "# Documentation Site" > README.md
    git add README.md
    git config user.name "docs-bot"
    git config user.email "docs-bot@users.noreply.github.com"
    git commit -m "Initial gh-pages branch"
    cd - > /dev/null
  fi
fi

echo "Deploying to gh-pages branch..."

# Clear target directory (preserve .git)
find "$DEPLOY_DIR" -mindepth 1 -maxdepth 1 ! -name ".git" -exec rm -rf {} +

# Copy built site to deployment target
cp -r "$BUILD_DIR/staged"/* "$DEPLOY_DIR/"

echo "Files copied to gh-pages branch."
echo "To commit and push changes, run: ./scripts/docs/06-push-remote.sh"