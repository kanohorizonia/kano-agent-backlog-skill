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
  "$KOB_CMD" >> "$BUILD_DIR/content_quartz/cli/commands.md" 2>/dev/null || echo "Command not available" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo '```' >> "$BUILD_DIR/content_quartz/cli/commands.md"
else
  echo "---" > "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "title: CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "---" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "# CLI Commands" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "" >> "$BUILD_DIR/content_quartz/cli/commands.md"
  echo "kob command not available" >> "$BUILD_DIR/content_quartz/cli/commands.md"
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
