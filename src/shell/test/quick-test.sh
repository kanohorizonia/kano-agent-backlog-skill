#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

show_help() {
  cat <<'EOF'
Usage: quick-test.sh [preset] [config]

Runs the native C++ smoke tests. Legacy pytest coverage is no longer part of
the executable test gate.

Examples:
  bash src/shell/test/quick-test.sh
  bash src/shell/test/quick-test.sh windows-ninja-msvc Debug
EOF
}

case "${1:-}" in
  --help|-h)
    show_help
    exit 0
    ;;
  --verbose|-v)
    shift
    ;;
esac

echo "=== kano-agent-backlog-skill quick test (native) ==="
echo "  Skill root: $SKILL_ROOT"

exec "$SCRIPT_DIR/native-test.sh" "$@"
