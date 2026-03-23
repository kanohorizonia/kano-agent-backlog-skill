#!/bin/bash
set -euo pipefail

# Commit and push changes to remote gh-pages branch
# Run after deploy-quartz.sh to commit and push to GitHub
#
# Usage: 
#   06-push-remote.sh [DEPLOY_DIR] [COMMIT_MESSAGE]
#   If no arguments provided, auto-detect paths for local usage

# Parse arguments or auto-detect paths
if [ $# -ge 1 ]; then
  # Parameterized mode: use provided path
  DEPLOY_DIR="$1"
  COMMIT_MESSAGE="${2:-Deploy docs site}"
  echo "Using provided path: $DEPLOY_DIR"
else
  # Local mode: auto-detect repository root
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
  DEPLOY_DIR="$REPO_ROOT/_ws/deploy/gh-pages"
  COMMIT_MESSAGE="Deploy docs site from local build"
  echo "Auto-detected path: $DEPLOY_DIR"
fi

# Validate workspace structure
if [ ! -d "$DEPLOY_DIR/.git" ]; then
  echo "Error: Deploy git repository not found: $DEPLOY_DIR/.git"
  exit 1
fi

echo "Committing and pushing to remote gh-pages branch..."

cd "$DEPLOY_DIR"

# Configure git
git config user.name "docs-bot"
git config user.email "docs-bot@users.noreply.github.com"

# Stage all changes
git add -A

# Check if there are changes to commit
if git diff --cached --quiet; then
  echo "No changes to commit."
  exit 0
fi

# Commit changes
git commit -m "$COMMIT_MESSAGE"

# Push to remote
git push origin gh-pages

echo "Successfully committed and pushed to gh-pages branch!"
echo "Documentation site should be available at:"
echo "https://dorgonman.github.io/kano-agent-backlog-skill/"