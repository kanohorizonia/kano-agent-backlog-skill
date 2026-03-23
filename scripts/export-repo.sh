#!/bin/bash

# Auto-detect repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Get repository name
REPO_NAME=$(basename "$REPO_ROOT")

# Create archives directory (will be gitignored)
ARCHIVE_DIR="$REPO_ROOT/_archives"
mkdir -p "$ARCHIVE_DIR"

# Get git revision number
REV_NUM=$(git rev-list --count HEAD)

# Archive filename
ARCHIVE_FILE="${REPO_NAME}_Rev${REV_NUM}.tar"
ARCHIVE_PATH="$ARCHIVE_DIR/$ARCHIVE_FILE"

echo "Creating archive: $ARCHIVE_FILE"
echo "Repository: $REPO_NAME"
echo "Revision: $REV_NUM"

# Use git ls-files to get only tracked files (respects .gitignore)
git ls-files | tar -cf "$ARCHIVE_PATH" -T -

echo "Archive created: $ARCHIVE_PATH"
echo "Size: $(du -h "$ARCHIVE_PATH" | cut -f1)"