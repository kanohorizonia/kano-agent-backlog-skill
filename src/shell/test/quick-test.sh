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

case $# in
  0) exec "$SCRIPT_DIR/native-test.sh" "" "" quick ;;
  1) exec "$SCRIPT_DIR/native-test.sh" "$1" "" quick ;;
  2) exec "$SCRIPT_DIR/native-test.sh" "$1" "$2" quick ;;
  *)
    preset="$1"
    config="$2"
    shift 2
    exec "$SCRIPT_DIR/native-test.sh" "$preset" "$config" quick "$@"
    ;;
esac
