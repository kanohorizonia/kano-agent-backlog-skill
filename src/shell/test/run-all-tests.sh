#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

show_help() {
  cat <<'EOF'
Usage: run-all-tests.sh [options] [preset] [config]

Runs the native C++ test suite plus native runtime gate. Legacy pytest coverage
is no longer part of the supported executable contract.

Options:
  --cleanup   Accepted for legacy callers; native tests do not create coverage output here.
  --verbose   Forwarded to ctest on non-Windows lanes when possible.
  --help      Show this help.

Examples:
  bash src/shell/test/run-all-tests.sh
  bash src/shell/test/run-all-tests.sh windows-ninja-msvc Debug
EOF
}

args=()
ctest_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cleanup)
      shift
      ;;
    --verbose|-v)
      ctest_args+=(--verbose)
      shift
      ;;
    --help|-h)
      show_help
      exit 0
      ;;
    *)
      args+=("$1")
      shift
      ;;
  esac
done

echo "=== kano-agent-backlog-skill full test suite (native) ==="
echo "  Skill root: $SKILL_ROOT"

"$SCRIPT_DIR/native-test.sh" "${args[@]}" "${ctest_args[@]}"
"$SCRIPT_DIR/native-runtime-gate.sh"

echo "Native full test suite passed."
