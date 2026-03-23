#!/bin/bash
set -euo pipefail

# Main deployment script - runs all documentation deployment steps in sequence
# This script orchestrates the complete documentation build and deployment process
#
# Usage: 
#   build-and-deploy.sh [--ci] [REPO_ROOT]
#   --ci: Skip workspace setup (for CI environments)
#   If no REPO_ROOT provided, auto-detect for local usage

# Parse arguments
CI_MODE=false
REPO_ROOT=""

while [[ $# -gt 0 ]]; do
  case $1 in
    --ci)
      CI_MODE=true
      shift
      ;;
    *)
      REPO_ROOT="$1"
      shift
      ;;
  esac
done

# Auto-detect repository root if not provided
if [ -z "$REPO_ROOT" ]; then
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi

echo "=== Documentation Deployment Pipeline ==="
echo "Repository root: $REPO_ROOT"
echo "CI mode: $CI_MODE"
echo ""

# Step 1: Setup workspace (skip in CI mode)
if [ "$CI_MODE" = false ]; then
  echo "Step 1: Setting up workspace..."
  bash "$REPO_ROOT/scripts/docs/01-setup-workspace.sh"
  echo ""
else
  echo "Step 1: Skipping workspace setup (CI mode)"
  echo ""
fi

# Step 2: Prepare content
echo "Step 2: Preparing documentation content..."
if [ "$CI_MODE" = true ]; then
  # CI mode: use explicit paths with YAML config
  bash "$REPO_ROOT/_ws/src/demo/scripts/docs/02-prepare-content.sh" \
    "$REPO_ROOT" \
    "$REPO_ROOT/_ws/src/demo" \
    "$REPO_ROOT/_ws/src/skill" \
    "$REPO_ROOT/_ws/build"
else
  # Local mode: use YAML config with auto-detect paths
  bash "$REPO_ROOT/scripts/docs/02-prepare-content.sh"
fi
echo ""

# Step 3: Build site
echo "Step 3: Building Quartz site..."
if [ "$CI_MODE" = true ]; then
  # CI mode: use parameterized script
  bash "$REPO_ROOT/_ws/src/demo/scripts/docs/03-build-site.sh" \
    "$REPO_ROOT" \
    "$REPO_ROOT/_ws/src/quartz" \
    "$REPO_ROOT/_ws/build" \
    "$REPO_ROOT/_ws/src/demo/scripts/docs/config/quartz.config.ts"
else
  # Local mode: use parameterized script with auto-detect
  bash "$REPO_ROOT/scripts/docs/03-build-site.sh"
fi
echo ""

# Step 4: Deploy MkDocs API documentation
echo "Step 4: Deploying MkDocs API documentation..."
if [ "$CI_MODE" = true ]; then
  # CI mode: use parameterized script
  bash "$REPO_ROOT/_ws/src/demo/scripts/docs/04-deploy-mkdocs.sh" \
    "$REPO_ROOT/_ws/build" \
    "$REPO_ROOT/_ws/src/skill" \
    "$REPO_ROOT/_ws/src/demo/scripts/docs/config/mkdocs.yml"
else
  # Local mode: use auto-detect
  bash "$REPO_ROOT/scripts/docs/04-deploy-mkdocs.sh"
fi
echo ""

# Step 5: Deploy to local gh-pages branch
echo "Step 5: Deploying to local gh-pages branch..."
if [ "$CI_MODE" = true ]; then
  # CI mode: use parameterized script
  bash "$REPO_ROOT/_ws/src/demo/scripts/docs/05-deploy-quartz.sh" \
    "$REPO_ROOT/_ws/build" \
    "$REPO_ROOT/_ws/deploy/gh-pages" \
    "Deploy docs site from GitHub Actions (${GITHUB_SHA:-unknown})"
else
  # Local mode: use parameterized script with auto-detect
  bash "$REPO_ROOT/scripts/docs/05-deploy-quartz.sh"
fi
echo ""

# Step 6: Push to remote
echo "Step 6: Committing and pushing to remote gh-pages branch..."
if [ "$CI_MODE" = true ]; then
  # CI mode: use parameterized script with auto-push
  bash "$REPO_ROOT/_ws/src/demo/scripts/docs/06-push-remote.sh" \
    "$REPO_ROOT/_ws/deploy/gh-pages" \
    "Deploy docs site from GitHub Actions (${GITHUB_SHA:-unknown})"
else
  # Local mode: use parameterized script with auto-detect
  bash "$REPO_ROOT/scripts/docs/06-push-remote.sh"
fi
echo ""

echo "=== Deployment Complete ==="
echo ""
echo "Documentation site deployed successfully!"
echo "Site URL: https://dorgonman.github.io/kano-agent-backlog-skill/"
echo ""
if [ "$CI_MODE" = false ]; then
  echo "To clean up workspace: rm -rf _ws"
fi
