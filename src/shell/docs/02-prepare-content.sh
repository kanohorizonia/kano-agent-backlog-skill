#!/bin/bash
set -euo pipefail

# Enhanced content preparation with YAML-based configuration
# Supports content mapping, navigation structure, and automatic index generation

# Parse arguments or auto-detect paths
if [ $# -eq 4 ]; then
  REPO_ROOT="$1"
  DEMO_DIR="$2"
  SKILL_DIR="$3"
  BUILD_DIR="$4"
  echo "Using provided paths (CI mode)"
else
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
  DEMO_DIR="$REPO_ROOT/_ws/src/demo"
  SKILL_DIR="$REPO_ROOT/_ws/src/skill"
  BUILD_DIR="$REPO_ROOT/_ws/build"
  echo "Auto-detected paths (local mode)"
fi

mkdir -p "$BUILD_DIR/content_quartz"

# Check prerequisites
echo "Checking prerequisites..."
PREREQ_MISSING=false

if [ ! -x "$REPO_ROOT/scripts/kob" ] && ! command -v kob >/dev/null 2>&1; then
  echo "WARNING: kob command surface not available - CLI docs will be limited"
fi

if [ "$PREREQ_MISSING" = true ]; then
  echo "ERROR: Missing required prerequisites"
  exit 1
fi

# Clean previous build artifacts
echo "Cleaning previous build artifacts..."
rm -rf "$BUILD_DIR/content_quartz"/*
rm -rf "$BUILD_DIR/content_mkdocs"/*
rm -rf "$BUILD_DIR/staged"/*

copy_doc() {
  local source_path="$1"
  local target_path="$2"
  if [ ! -f "$source_path" ]; then
    echo "WARNING: docs source missing: $source_path" >&2
    return 0
  fi
  mkdir -p "$(dirname "$target_path")"
  cp "$source_path" "$target_path"
}

copy_glob_docs() {
  local source_dir="$1"
  local target_dir="$2"
  local file
  [ -d "$source_dir" ] || return 0
  mkdir -p "$target_dir"
  shopt -s nullglob
  for file in "$source_dir"/*.md "$source_dir"/*.json "$source_dir"/*.sql; do
    [ -f "$file" ] || continue
    cp "$file" "$target_dir/$(basename "$file")"
  done
  shopt -u nullglob
}

write_kob_help() {
  local output_path="$1"
  local fallback_path="$SKILL_DIR/docs/cli/kob-help.txt"
  local help_tmp="$BUILD_DIR/kob-help.tmp"

  if [ -n "${KOB_CMD:-}" ] && "$KOB_CMD" --help > "$help_tmp" 2>/dev/null; then
    cat "$help_tmp" >> "$output_path"
    rm -f "$help_tmp"
    return 0
  fi
  rm -f "$help_tmp"

  if [ -f "$fallback_path" ]; then
    cat "$fallback_path" >> "$output_path"
    return 0
  fi

  echo "kob help snapshot not available. Build native with: bash scripts/kob self build" >> "$output_path"
}

echo "Copying published documentation content..."
copy_doc "$SKILL_DIR/docs/index.md" "$BUILD_DIR/content_quartz/index.md"
copy_doc "$SKILL_DIR/README.md" "$BUILD_DIR/content_quartz/skill/readme.md"
copy_doc "$SKILL_DIR/SKILL.md" "$BUILD_DIR/content_quartz/skill/guide.md"
copy_doc "$SKILL_DIR/REFERENCE.md" "$BUILD_DIR/content_quartz/skill/reference.md"
copy_doc "$SKILL_DIR/CHANGELOG.md" "$BUILD_DIR/content_quartz/skill/changelog.md"

copy_doc "$SKILL_DIR/docs/quick-start.md" "$BUILD_DIR/content_quartz/guides/quick-start.md"
copy_doc "$SKILL_DIR/docs/agent-quick-start.md" "$BUILD_DIR/content_quartz/guides/agent-quick-start.md"
copy_doc "$SKILL_DIR/docs/installation.md" "$BUILD_DIR/content_quartz/guides/installation.md"
copy_doc "$SKILL_DIR/docs/configuration.md" "$BUILD_DIR/content_quartz/guides/configuration.md"
copy_doc "$SKILL_DIR/docs/usage-examples.md" "$BUILD_DIR/content_quartz/guides/usage-examples.md"
copy_doc "$SKILL_DIR/docs/workset.md" "$BUILD_DIR/content_quartz/guides/workset.md"
copy_doc "$SKILL_DIR/docs/topic.md" "$BUILD_DIR/content_quartz/guides/topic.md"
copy_doc "$SKILL_DIR/docs/version-policy.md" "$BUILD_DIR/content_quartz/guides/version-policy.md"
copy_doc "$SKILL_DIR/docs/release-channels.md" "$BUILD_DIR/content_quartz/guides/release-channels.md"
copy_doc "$SKILL_DIR/docs/design/native-cli-direction.md" "$BUILD_DIR/content_quartz/guides/native-cli-direction.md"
copy_doc "$SKILL_DIR/docs/demo-maintenance.md" "$BUILD_DIR/content_quartz/guides/demo-maintenance.md"
copy_doc "$SKILL_DIR/docs/codex-for-oss.md" "$BUILD_DIR/content_quartz/guides/codex-for-oss.md"
copy_doc "$SKILL_DIR/docs/experimental-features.md" "$BUILD_DIR/content_quartz/guides/experimental-features.md"
copy_doc "$SKILL_DIR/docs/tokenizer-quickstart.md" "$BUILD_DIR/content_quartz/guides/tokenizer-quickstart.md"
copy_doc "$SKILL_DIR/docs/tokenizer-adapters.md" "$BUILD_DIR/content_quartz/guides/tokenizer-adapters.md"
copy_doc "$SKILL_DIR/docs/tokenizer-configuration.md" "$BUILD_DIR/content_quartz/guides/tokenizer-configuration.md"
copy_doc "$SKILL_DIR/docs/tokenizer-cli-reference.md" "$BUILD_DIR/content_quartz/guides/tokenizer-cli-reference.md"
copy_doc "$SKILL_DIR/docs/tokenizer-troubleshooting.md" "$BUILD_DIR/content_quartz/guides/tokenizer-troubleshooting.md"
copy_doc "$SKILL_DIR/docs/tokenizer-performance.md" "$BUILD_DIR/content_quartz/guides/tokenizer-performance.md"

copy_doc "$SKILL_DIR/docs/maintainer-automation.md" "$BUILD_DIR/content_quartz/automation/maintainer-automation.md"
copy_doc "$SKILL_DIR/src/shell/docs/README.md" "$BUILD_DIR/content_quartz/automation/docs-pipeline.md"

copy_glob_docs "$SKILL_DIR/docs/releases" "$BUILD_DIR/content_quartz/releases"
copy_doc "$SKILL_DIR/CHANGELOG.md" "$BUILD_DIR/content_quartz/releases/changelog.md"

copy_glob_docs "$SKILL_DIR/references" "$BUILD_DIR/content_quartz/references"

# Generate CLI and API docs first
echo "Generating CLI documentation..."
mkdir -p "$BUILD_DIR/content_quartz/cli"

KOB_CMD=""
if [ -x "$REPO_ROOT/scripts/kob" ]; then
  KOB_CMD="$REPO_ROOT/scripts/kob"
elif command -v kob >/dev/null 2>&1; then
  KOB_CMD="kob"
fi

if [ -n "$KOB_CMD" ]; then
  echo "---" > "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "title: CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "---" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "# CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "Complete command-line interface documentation for the kob tool." >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "## kob" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo '```' >> "$BUILD_DIR/content_quartz/cli/commands.md"
  write_kob_help "$BUILD_DIR/content_quartz/cli/commands.md"
  echo '```' >> "$BUILD_DIR/content_quartz/cli/commands.md"
else
  echo "---" > "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "title: CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "---" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "# CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo '```' >> "$BUILD_DIR/content_quartz/cli/commands.md"
  write_kob_help "$BUILD_DIR/content_quartz/cli/commands.md"
  echo '```' >> "$BUILD_DIR/content_quartz/cli/commands.md"
fi

# Generate API documentation with MkDocs
echo "Generating API documentation..."
mkdir -p "$BUILD_DIR/content_quartz/api"

# Create API overview
echo "---" > "$BUILD_DIR/content_quartz/api/overview.md"
echo "title: API Overview" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "---" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "# API Overview" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "Native executable API reference for Kano Agent Backlog Skill." >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "## Public Contract" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **scripts/kob** - Repo-local launcher for the native CLI" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **scripts/kano-backlog** - Compatibility launcher for the native CLI" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **kano-backlog native binary** - C++ executable command surface" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "Python import APIs are not a supported public runtime contract for this native milestone." >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"

# Create native API entry point
echo "---" > "$BUILD_DIR/content_quartz/api/native.md"
echo "title: Native Executable API" >> "$BUILD_DIR/content_quartz/api/native.md"
echo "---" >> "$BUILD_DIR/content_quartz/api/native.md"
echo "" >> "$BUILD_DIR/content_quartz/api/native.md"
echo "# Native Executable API" >> "$BUILD_DIR/content_quartz/api/native.md"
echo "" >> "$BUILD_DIR/content_quartz/api/native.md"
echo "Run \`scripts/kob --help\` for the authoritative command surface." >> "$BUILD_DIR/content_quartz/api/native.md"
echo "" >> "$BUILD_DIR/content_quartz/api/native.md"

# Process content with YAML config supporting multiple source directories
if [ -d "$DEMO_DIR" ]; then
  echo "YAML-driven docs expansion is retired from the native executable docs pipeline."
fi

echo "Enhanced content preparation completed successfully"
