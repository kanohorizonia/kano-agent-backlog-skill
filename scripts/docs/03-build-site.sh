#!/bin/bash
set -euo pipefail

# Build Quartz site locally
# Run after docs-prepare-quartz.sh to build the static site
#
# Usage: 
#   03-build-site.sh [REPO_ROOT] [QUARTZ_DIR] [BUILD_DIR] [CONFIG_FILE]
#   If no arguments provided, auto-detect paths for local usage

# Parse arguments or auto-detect paths
if [ $# -eq 4 ]; then
  # Parameterized mode: use provided paths
  REPO_ROOT="$1"
  QUARTZ_DIR="$2"
  BUILD_DIR="$3"
  CONFIG_FILE="$4"
  echo "Using provided paths"
else
  # Local mode: auto-detect repository root
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
  QUARTZ_DIR="$REPO_ROOT/_ws/src/quartz"
  BUILD_DIR="$REPO_ROOT/_ws/build"
  CONFIG_FILE="$SCRIPT_DIR/config/quartz.config.ts"
  echo "Auto-detected paths"
fi

# Validate workspace structure
if [ ! -d "$QUARTZ_DIR" ]; then
  echo "Error: Quartz directory not found: $QUARTZ_DIR"
  exit 1
fi

if [ ! -d "$BUILD_DIR/content_quartz" ]; then
  echo "Error: Build content directory not found: $BUILD_DIR/content_quartz"
  exit 1
fi

echo "Building Quartz site..."

# Clean output directory
echo "Cleaning output directory..."
rm -rf "$BUILD_DIR/staged"/*
mkdir -p "$BUILD_DIR/staged"

# Install dependencies
echo "Installing Quartz dependencies..."
cd "$QUARTZ_DIR"
npm ci

# Apply custom configuration
echo "Applying custom Quartz configuration..."
cp "$CONFIG_FILE" ./quartz.config.ts

# Install tokyo-night theme
echo "Installing tokyo-night theme..."
npm install --save-dev shiki-themes

# Build static site
echo "Building static site..."
CONTENT_DIR="$BUILD_DIR/content_quartz"
OUTPUT_DIR="$BUILD_DIR/staged"
echo "Content directory: $CONTENT_DIR"
echo "Output directory: $OUTPUT_DIR"
npx quartz build --directory "$CONTENT_DIR" --output "$OUTPUT_DIR"

echo "Build completed successfully!"
echo "Static site available in: $OUTPUT_DIR"