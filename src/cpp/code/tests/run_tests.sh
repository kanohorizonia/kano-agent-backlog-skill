#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

export CI="${CI:-true}"
export KANO_BACKLOG_NONINTERACTIVE=1
export KANO_TEST_NONINTERACTIVE=1
export GIT_TERMINAL_PROMPT=0
PRESET="${1:-windows-ninja-msvc-release}"
CONFIG="${2:-Debug}"
LANE_MODE="${3:-default}"
RUNNER_PRESET="$PRESET"
shift $(( $# > 0 ? 1 : 0 )) || true
shift $(( $# > 0 ? 1 : 0 )) || true
shift $(( $# > 0 ? 1 : 0 )) || true

case "$LANE_MODE" in
  quick)
    export KANO_NATIVE_TEST_TIMEOUT_SECONDS="${KANO_NATIVE_TEST_TIMEOUT_SECONDS:-30}"
    ;;
  *)
    export KANO_NATIVE_TEST_TIMEOUT_SECONDS="${KANO_NATIVE_TEST_TIMEOUT_SECONDS:-120}"
    ;;
esac

case "$RUNNER_PRESET" in
  *-debug|*-release|*-relwithdebinfo|*-minsizerel)
    RUNNER_PRESET="${RUNNER_PRESET%-debug}"
    RUNNER_PRESET="${RUNNER_PRESET%-release}"
    RUNNER_PRESET="${RUNNER_PRESET%-relwithdebinfo}"
    RUNNER_PRESET="${RUNNER_PRESET%-minsizerel}"
    ;;
esac

write_placeholder_junit() {
  local output_path="${KANO_TEST_XML:-}"
  if [[ -z "$output_path" || -f "$output_path" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "$output_path")"
  cat > "$output_path" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="kano-agent-backlog-skill.native.${LANE_MODE}" tests="0" failures="0" errors="0" skipped="0" time="0" />
</testsuites>
EOF
}

echo "=== kano-backlog code/tests wrapper ==="
echo "  CPP_ROOT: $CPP_ROOT"
echo "  PRESET: $PRESET"
echo "  CONFIG: $CONFIG"
echo "  LANE: $LANE_MODE"
echo "  TIMEOUT_SECONDS: $KANO_NATIVE_TEST_TIMEOUT_SECONDS"

bash "$CPP_ROOT/../shell/test/native-test.sh" "$RUNNER_PRESET" "$CONFIG" "$LANE_MODE" "$@"
write_placeholder_junit
