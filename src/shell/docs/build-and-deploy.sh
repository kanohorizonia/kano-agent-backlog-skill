#!/bin/bash
set -euo pipefail

# Main documentation pipeline script
# Builds and stages the documentation site by default.
# Local deploy prep and remote publish are explicit opt in steps.
#
# Usage: 
#   build-and-deploy.sh [--ci] [--prep-deploy] [--push] [REPO_ROOT]
#   --ci: CI friendly mode
#   --prep-deploy: populate _ws/deploy/gh-pages locally
#   --push: commit and push the prepared gh-pages working tree
#   If no REPO_ROOT provided, auto-detect for local usage

# Parse arguments
CI_MODE=false
PREP_DEPLOY=false
PUSH_REMOTE=false
REPO_ROOT=""

while [[ $# -gt 0 ]]; do
  case $1 in
    --ci)
      CI_MODE=true
      shift
      ;;
    --prep-deploy)
      PREP_DEPLOY=true
      shift
      ;;
    --push)
      PREP_DEPLOY=true
      PUSH_REMOTE=true
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
  REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
fi

echo "=== Documentation Build Pipeline ==="
echo "Repository root: $REPO_ROOT"
echo "CI mode: $CI_MODE"
echo "Prepare local deploy tree: $PREP_DEPLOY"
echo "Push remote: $PUSH_REMOTE"
echo ""

# Step 1: Setup workspace
echo "Step 1: Setting up workspace..."
bash "$REPO_ROOT/src/shell/docs/01-setup-workspace.sh"
echo ""

# Step 2: Prepare content
echo "Step 2: Preparing documentation content..."
bash "$REPO_ROOT/src/shell/docs/02-prepare-content.sh" \
  "$REPO_ROOT" \
  "$REPO_ROOT/_ws/src/demo" \
  "$REPO_ROOT/_ws/src/skill" \
  "$REPO_ROOT/_ws/build"
echo ""

# Step 3: Build site
echo "Step 3: Building Quartz site..."
bash "$REPO_ROOT/src/shell/docs/03-build-site.sh" \
  "$REPO_ROOT" \
  "$REPO_ROOT/_ws/src/quartz" \
  "$REPO_ROOT/_ws/build" \
  "$REPO_ROOT/src/shell/docs/config/quartz.config.ts"
echo ""

# Step 4: Deploy MkDocs API documentation
echo "Step 4: Deploying MkDocs API documentation..."
bash "$REPO_ROOT/src/shell/docs/04-deploy-mkdocs.sh" \
  "$REPO_ROOT/_ws/build" \
  "$REPO_ROOT/_ws/src/skill" \
  "$REPO_ROOT/src/shell/docs/config/mkdocs.yml"
echo ""

if [[ -n "${KANO_PUBLIC_REPORT_SOURCE_DIR:-}" || -n "${KANO_PUBLIC_REPORT_SOURCE_URL:-}" ]]; then
  echo "Step 4b: Staging public report slots..."
  bash "$REPO_ROOT/src/shell/docs/stage-public-report-slots.sh" \
    "$REPO_ROOT/_ws/build"
  echo ""
fi

if [ "$PREP_DEPLOY" = true ]; then
  echo "Step 5: Preparing local gh-pages working tree..."
  export KANO_SITE_STAGING_ROOT="${KANO_SITE_STAGING_ROOT:-$REPO_ROOT/_ws/build/staged}"
  bash "$REPO_ROOT/src/shell/docs/05-deploy-quartz.sh" \
    "$REPO_ROOT/_ws/build" \
    "$REPO_ROOT/_ws/deploy/gh-pages" \
    "Prepare docs site from ${GITHUB_SHA:-local-build}"
  echo ""
fi

if [ "$PUSH_REMOTE" = true ]; then
  echo "Step 6: Committing and pushing to remote gh-pages branch..."
  bash "$REPO_ROOT/src/shell/docs/06-push-remote.sh" \
    "$REPO_ROOT/_ws/deploy/gh-pages" \
    "Deploy docs site from ${GITHUB_SHA:-local-build}"
  echo ""
fi

echo "=== Build Complete ==="
echo ""
echo "Documentation site staged successfully!"
echo "Staged site directory: $REPO_ROOT/_ws/build/staged"
echo "Site URL: https://agentskill-backlog.kanohorizonia.com/"
echo ""
if [ "$PREP_DEPLOY" = false ]; then
  echo "Local deploy prep was skipped. Use --prep-deploy if you need _ws/deploy/gh-pages."
fi
if [ "$PUSH_REMOTE" = false ]; then
  echo "Remote publish was skipped. Use --push only when you intend to publish."
fi
if [ "$CI_MODE" = false ]; then
  echo "To clean up workspace: rm -rf _ws"
fi
