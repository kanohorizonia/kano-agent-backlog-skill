#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

for python_bin in python3 python py; do
  if command -v "$python_bin" >/dev/null 2>&1; then
    exec "$python_bin" "$SCRIPT_DIR/post_release_verify.py" asset-recheck "$@"
  fi
done

echo "python3, python, or py is required for release asset recheck" >&2
exit 127
