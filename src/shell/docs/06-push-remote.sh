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
  REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
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

TARGET_BRANCH="${KANO_GITHUB_PAGES_BRANCH:-gh-pages}"
REMOTE_URL="${KANO_GITHUB_PAGES_REPO_URL:-}"
GIT_ASKPASS_FILE=""

cleanup_git_askpass() {
  if [[ -n "$GIT_ASKPASS_FILE" && -f "$GIT_ASKPASS_FILE" ]]; then
    rm -f "$GIT_ASKPASS_FILE"
  fi
}
trap cleanup_git_askpass EXIT

setup_git_noninteractive_auth() {
  export GIT_TERMINAL_PROMPT=0
  export GCM_INTERACTIVE=never
  if [[ -n "${KANO_GITHUB_PAGES_REPO_TOKEN:-}" ]]; then
    GIT_ASKPASS_FILE="$(mktemp)"
    cat > "$GIT_ASKPASS_FILE" <<'SH'
#!/bin/sh
case "$1" in
  *Username*|*username*)
    printf '%s\n' "${KANO_GITHUB_PAGES_REPO_USER:-x-access-token}"
    ;;
  *)
    printf '%s\n' "$KANO_GITHUB_PAGES_REPO_TOKEN"
    ;;
esac
SH
    chmod 700 "$GIT_ASKPASS_FILE"
    export GIT_ASKPASS="$GIT_ASKPASS_FILE"
    export SSH_ASKPASS="$GIT_ASKPASS_FILE"
  fi
}

setup_git_noninteractive_auth

if [[ -n "$REMOTE_URL" ]]; then
  git remote set-url origin "$REMOTE_URL"
fi

# Configure git
git config user.name "${KANO_GITHUB_PAGES_GIT_USER_NAME:-docs-bot}"
git config user.email "${KANO_GITHUB_PAGES_GIT_USER_EMAIL:-docs-bot@users.noreply.github.com}"

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
git push origin "HEAD:$TARGET_BRANCH"

echo "Successfully committed and pushed to $TARGET_BRANCH branch!"
echo "Documentation site should be available at:"
echo "https://agentskill-backlog.kanohorizonia.com/"
