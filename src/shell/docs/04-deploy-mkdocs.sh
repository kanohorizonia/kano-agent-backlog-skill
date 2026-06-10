#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-}"
if [[ -z "$BUILD_DIR" ]]; then
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
  BUILD_DIR="$REPO_ROOT/_ws/build"
fi

if [[ ! -d "$BUILD_DIR/staged" ]]; then
  echo "ERROR: Build staged directory not found: $BUILD_DIR/staged" >&2
  echo "Run Quartz build first" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/staged/api-docs"
cat > "$BUILD_DIR/staged/api-docs/index.html" <<'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Native API</title>
</head>
<body>
  <h1>Native Executable API</h1>
  <p>Python API documentation is retired for the native executable milestone.</p>
  <p>Use the repo-local <code>scripts/kob</code> launcher and <code>kano-backlog --help</code> for the supported command surface.</p>
</body>
</html>
EOF

echo "MkDocs Python API generation skipped; native API placeholder written."
