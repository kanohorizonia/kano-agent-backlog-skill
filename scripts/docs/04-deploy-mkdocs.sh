#!/bin/bash
set -euo pipefail

# Deploy MkDocs API documentation as part of main site
# Integrates API documentation into the Quartz-built site
#
# Usage: 
#   deploy-mkdocs.sh [BUILD_DIR] [SKILL_DIR] [CONFIG_FILE]
#   If no arguments provided, auto-detect paths for local usage

# Parse arguments or auto-detect paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ $# -eq 3 ]; then
  BUILD_DIR="$1"
  SKILL_DIR="$2"
  CONFIG_FILE="$3"
  echo "Using provided paths (CI mode)"

  # In CI, BUILD_DIR is expected to be: <workspace>/_ws/build
  # Derive the workspace root so config resolution works consistently.
  REPO_ROOT="$(cd "$(dirname "$(dirname "$BUILD_DIR")")" && pwd)"
else
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
  BUILD_DIR="$REPO_ROOT/_ws/build"
  SKILL_DIR="$REPO_ROOT/_ws/src/skill"
  
  # Load config path helper
  source "$SCRIPT_DIR/help/config-paths.sh"
  
  # Find mkdocs config
  CONFIG_FILE=$(find_config_file "mkdocs.yml" "$SCRIPT_DIR" "$REPO_ROOT")
  validate_config_file "$CONFIG_FILE" "mkdocs.yml" || exit 1
  
  echo "Auto-detected paths (local mode)"
  echo "Using mkdocs config: $CONFIG_FILE"
fi

echo "Deploying MkDocs API documentation..."

# Check prerequisites
if ! command -v mkdocs >/dev/null 2>&1; then
  echo "WARNING: MkDocs not available - skipping API docs generation"
  echo "Install with: pip install mkdocs mkdocs-material"
  return 0 2>/dev/null || exit 0
fi

if [ ! -d "$SKILL_DIR" ]; then
  echo "WARNING: Skill directory not found: $SKILL_DIR - skipping API docs"
  return 0 2>/dev/null || exit 0
fi

if [ ! -d "$BUILD_DIR/staged" ]; then
  echo "ERROR: Build staged directory not found: $BUILD_DIR/staged"
  echo "Run Quartz build first"
  exit 1
fi

# Create temporary MkDocs project
TEMP_MKDOCS="$BUILD_DIR/content_mkdocs"
rm -rf "$TEMP_MKDOCS"
mkdir -p "$TEMP_MKDOCS/docs/api"

# Find publish config
source "$SCRIPT_DIR/help/config-paths.sh"
API_CONFIG=$(find_config_file "publish.config.yml" "$SCRIPT_DIR" "$REPO_ROOT")
validate_config_file "$API_CONFIG" "publish.config.yml" || exit 1
echo "Using publish config: $API_CONFIG"

echo "Generating MkDocs configuration and content..."

# Copy static MkDocs configuration
cp "$CONFIG_FILE" "$TEMP_MKDOCS/"
echo "Copied MkDocs config to: $TEMP_MKDOCS/mkdocs.yml"

# Generate dynamic content and navigation using Python
python "$SCRIPT_DIR/help/mkdocs-content-generator.py" "$API_CONFIG" "$TEMP_MKDOCS/mkdocs.yml" "$TEMP_MKDOCS/docs"

# Show generated MkDocs config if it exists
if [ -f "$TEMP_MKDOCS/mkdocs.yml" ]; then
  echo "Generated MkDocs configuration:"
  echo "Config file: $TEMP_MKDOCS/mkdocs.yml"
else
  echo "ERROR: MkDocs config generation failed - file not found: $TEMP_MKDOCS/mkdocs.yml"
  exit 1
fi

# Build MkDocs site
echo "Building MkDocs site..."
cd "$TEMP_MKDOCS"
mkdocs build --site-dir "$BUILD_DIR/staged/api-docs" --quiet

echo "MkDocs API documentation integrated successfully!"
echo "API docs available at: /api-docs/"
echo "Temporary MkDocs project preserved at: $TEMP_MKDOCS"

# Cleanup (commented out to preserve temp files for debugging)
# rm -rf "$TEMP_MKDOCS"
