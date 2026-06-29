#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

for python_bin in python3 python py; do
  if command -v "$python_bin" >/dev/null 2>&1; then
    exec "$python_bin" "$SCRIPT_DIR/post_release_verify.py" homebrew-validate "$@"
  fi
done

echo "python3, python, or py is required for Homebrew validation" >&2
exit 127
