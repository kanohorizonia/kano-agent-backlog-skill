#!/bin/bash
set -euo pipefail

# Deploy built site to skill repo gh-pages branch
# Run after build-quartz-site.sh to deploy the documentation
#
# Usage: 
#   05-deploy-quartz.sh [BUILD_DIR] [DEPLOY_DIR] [COMMIT_MESSAGE]
#   If no arguments provided, auto-detect paths for local usage

SCRIPT_DIR_FOR_CONFIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR_FOR_CONFIG/config/build.json"
SKILL_REPO=$(grep -A6 '"repositories"' "$CONFIG_FILE" | grep -o '"skill"[[:space:]]*:[[:space:]]*"[^"]*"' | cut -d'"' -f4)
SITE_URL=$(grep -A6 '"deployment"' "$CONFIG_FILE" | grep -o '"site_url"[[:space:]]*:[[:space:]]*"[^"]*"' | cut -d'"' -f4)

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
  REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
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
   if git ls-remote --heads "$SKILL_REPO" gh-pages | grep -q gh-pages; then
     echo "Cloning existing gh-pages branch..."
    git clone --branch gh-pages "$SKILL_REPO" "$DEPLOY_DIR"
   else
     echo "Creating new gh-pages branch..."
    git clone "$SKILL_REPO" "$DEPLOY_DIR"
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

normalize_public_report_root() {
  local value="${1:-reports/latest}"
  value="${value//\\//}"
  value="${value#/}"
  value="${value%/}"
  if [[ -z "$value" || "$value" == /* || "$value" == *".."* ]]; then
    echo "reports/latest"
    return 0
  fi
  echo "$value"
}

write_public_report_placeholder() {
  local slot_dir="$1"
  local title="$2"
  local policy="$3"
  mkdir -p "$slot_dir"
  cat > "$slot_dir/index.html" <<HTML
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${title}</title>
  <style>
    body { font-family: system-ui, -apple-system, Segoe UI, sans-serif; margin: 2rem; line-height: 1.5; color: #111827; background: #f8fafc; }
    main { max-width: 760px; }
    code { background: #e5e7eb; padding: 0.1rem 0.25rem; border-radius: 4px; }
    .status { display: inline-block; border: 1px solid #cbd5e1; border-radius: 999px; padding: 0.2rem 0.7rem; background: #fff; font-weight: 600; }
  </style>
</head>
<body>
<main>
  <p class="status">reserved</p>
  <h1>${title}</h1>
  <p>This stable public report slot is present, but no publishable report HTML was staged for this site build.</p>
  <p>Policy: ${policy}.</p>
  <p>Machine-readable slot metadata is available at <code>../public-report-slots.json</code>.</p>
</main>
</body>
</html>
HTML
}

stage_public_report_slots() {
  local public_root
  local source_root
  local target_root
  local policy
  public_root="$(normalize_public_report_root "${KANO_PUBLIC_REPORT_ROOT:-reports/latest}")"
  source_root="${KANO_SITE_STAGING_ROOT:-}"
  target_root="$DEPLOY_DIR/$public_root"
  policy="${KANO_PUBLIC_COVERAGE_SOURCE_POLICY:-source-free}"

  if [[ -n "$source_root" && -d "$source_root/$public_root" ]]; then
    echo "Copying public report slots from $source_root/$public_root"
    rm -rf "$target_root"
    mkdir -p "$(dirname "$target_root")"
    cp -r "$source_root/$public_root" "$target_root"
  else
    echo "No staged public report slots found; writing placeholders under $public_root"
    mkdir -p "$target_root"
    write_public_report_placeholder "$target_root/test-report" "Latest Test Report" "public-safe test report HTML may be published here"
    write_public_report_placeholder "$target_root/coverage-report" "Latest Coverage Report" "coverage publication uses ${policy}; private projects must publish source-free coverage"
    cat > "$target_root/public-report-slots.json" <<JSON
{
  "schemaVersion": 1,
  "kind": "kano-public-report-slots",
  "publicRoot": "$public_root",
  "policy": {
    "coverageSourcePolicy": "$policy",
    "coverageSourceAllowed": ${KANO_PUBLIC_COVERAGE_SOURCE_ALLOWED:-false},
    "unknownPolicyFailsClosed": true
  },
  "slots": [
    {"name": "test-report", "path": "$public_root/test-report", "status": "reserved"},
    {"name": "coverage-report", "path": "$public_root/coverage-report", "status": "reserved"}
  ]
}
JSON
  fi
}

stage_public_report_slots

# Restore CNAME for custom-domain GitHub Pages deployments.
SITE_HOST="${SITE_URL#*://}"
SITE_HOST="${SITE_HOST%%/*}"
SITE_HOST="${SITE_HOST%%:*}"
if [[ -n "$SITE_HOST" && "$SITE_HOST" != *.github.io ]]; then
  printf '%s\n' "$SITE_HOST" > "$DEPLOY_DIR/CNAME"
fi

echo "Files copied to local gh-pages working tree."
echo "To commit and push changes, run: ./src/shell/docs/06-push-remote.sh"
