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
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
  DEMO_DIR="$REPO_ROOT/_ws/src/demo"
  SKILL_DIR="$REPO_ROOT/_ws/src/skill"
  BUILD_DIR="$REPO_ROOT/_ws/build"
  echo "Auto-detected paths (local mode)"
fi

mkdir -p "$BUILD_DIR/content_quartz"

# Check prerequisites
echo "Checking prerequisites..."
PREREQ_MISSING=false

if ! command -v python >/dev/null 2>&1; then
  echo "ERROR: Python not found"
  PREREQ_MISSING=true
fi

if [ ! -x "$REPO_ROOT/kob" ] && ! command -v kob >/dev/null 2>&1; then
  echo "WARNING: kob command surface not available - CLI docs will be limited"
fi

if ! command -v mkdocs >/dev/null 2>&1; then
  echo "WARNING: MkDocs not available - API docs will be limited"
  echo "Install with: pip install mkdocs mkdocs-material mkdocstrings[python]"
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
if [ -x "$REPO_ROOT/kob" ]; then
  KOB_CMD="$REPO_ROOT/kob"
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
echo "Python API reference for Kano Agent Backlog Skill." >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "## Core Modules" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **kano_backlog_skill.core** - Core backlog management functionality" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **kano_backlog_skill.cli** - Command-line interface" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **kano_backlog_skill.models** - Data models and schemas" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- **kano_backlog_skill.services** - Business logic services" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "## Documentation" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "" >> "$BUILD_DIR/content_quartz/api/overview.md"
echo "- [Full API Documentation](mkdocs.md) - Complete API reference with detailed documentation" >> "$BUILD_DIR/content_quartz/api/overview.md"

# Create MkDocs entry point
echo "---" > "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "title: Full API Documentation" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "---" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "# Full API Documentation" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "Complete Python API reference generated with MkDocs and mkdocstrings." >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "## Access Full Documentation" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "The complete API documentation is available at:" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"
echo "**[https://dorgonman.github.io/kano-agent-backlog-skill/api-docs/](https://dorgonman.github.io/kano-agent-backlog-skill/api-docs/)**" >> "$BUILD_DIR/content_quartz/api/mkdocs.md"

# Process content with YAML config supporting multiple source directories
if [ -d "$DEMO_DIR" ]; then
  echo "Processing content with YAML config..."
  
  # Load config path helper
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  source "$SCRIPT_DIR/help/config-paths.sh"
  
  # Find publish config
  PUBLISH_CONFIG=$(find_config_file "publish.config.yml" "$SCRIPT_DIR" "$REPO_ROOT")
  if validate_config_file "$PUBLISH_CONFIG" "publish.config.yml"; then
    echo "Using publish config: $PUBLISH_CONFIG"
    python "$SCRIPT_DIR/help/process_yaml_config.py" "$REPO_ROOT/_ws/src" "$BUILD_DIR/content_quartz" "$PUBLISH_CONFIG"
  else
    echo "WARNING: publish.config.yml not found - skipping YAML processing"
  fi
fi

echo "Enhanced content preparation completed successfully"
