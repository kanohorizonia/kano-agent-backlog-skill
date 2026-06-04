#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

PRESET="${1:-windows-ninja-msvc-release}"
LANE_MODE="${2:-default}"
RUNNER_PRESET="$PRESET"

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
echo "  LANE: $LANE_MODE"

bash "$CPP_ROOT/build/script/windows/run_tests.sh" "$RUNNER_PRESET"
write_placeholder_junit
