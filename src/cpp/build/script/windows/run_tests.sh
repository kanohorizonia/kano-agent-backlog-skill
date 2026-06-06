#!/usr/bin/env bash
# =============================================================================
# kano-backlog C++ Test Runner
# =============================================================================
# Runs smoke tests for kano-backlog C++ code.
#
# Usage:
#   bash run_tests.sh [preset] [--with-e2e]
#
# Environment:
#   KOB_CPP_ROOT - C++ source root (default: script dir ../../)
#   KOB_BUILD_PRESET - Build preset to use (default: windows-ninja-msvc)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="${KOB_CPP_ROOT:-$(cd "$SCRIPT_DIR/../../.." && pwd)}"

PRESET="${1:-windows-ninja-msvc}"
WITH_E2E="${2:-}"

echo "=== kano-backlog test runner ==="
echo "  KOB_CPP_ROOT: $KOB_CPP_ROOT"
echo "  PRESET: $PRESET"

# Build directory and binary directory from preset
BUILD_DIR="$KOB_CPP_ROOT/out/obj/$PRESET"
BIN_DIR="$KOB_CPP_ROOT/out/bin/$PRESET/debug"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "[WARN] Build directory not found: $BUILD_DIR"
  echo "[INFO] Running default build first..."

  # Try to run build using the platform helper
  if [[ -f "$SCRIPT_DIR/../common/windows_preset_build.sh" ]]; then
    export KOB_CPP_ROOT
    source "$SCRIPT_DIR/../common/windows_preset_build.sh"
    kabsd_run_windows_build "$BUILD_DIR" "Debug" "Ninja Multi-Config" "x64"
  else
    echo "[ERROR] No build found and no build helper to run one." >&2
    exit 1
  fi
fi

echo ""
echo "=== Running smoke tests ==="

FAILED=0
TOTAL=0
SKIPPED=0
JUNIT_CASES=()

add_junit_case() {
  local test_name="$1"
  local status="$2"
  local message="${3:-}"
  TOTAL=$((TOTAL + 1))
  case "$status" in
    pass)
      JUNIT_CASES+=("    <testcase classname=\"kano.agent.backlog.native\" name=\"$test_name\" />")
      ;;
    skip)
      SKIPPED=$((SKIPPED + 1))
      JUNIT_CASES+=("    <testcase classname=\"kano.agent.backlog.native\" name=\"$test_name\"><skipped message=\"$message\" /></testcase>")
      ;;
    fail)
      JUNIT_CASES+=("    <testcase classname=\"kano.agent.backlog.native\" name=\"$test_name\"><failure message=\"$message\" /></testcase>")
      ;;
  esac
}

write_junit_report() {
  local output_path="${KANO_TEST_XML:-}"
  if [[ -z "$output_path" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "$output_path")"
  {
    printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'
    printf '%s\n' '<testsuites>'
    printf '  <testsuite name="kano-agent-backlog-skill.native" tests="%s" failures="%s" errors="0" skipped="%s" time="0">\n' "$TOTAL" "$FAILED" "$SKIPPED"
    printf '%s\n' "${JUNIT_CASES[@]}"
    printf '%s\n' '  </testsuite>'
    printf '%s\n' '</testsuites>'
  } > "$output_path"
  echo "[INFO] JUnit report written: $output_path"
}

for test_exe in backlog_core_smoke_test workitem_ops_smoke_test cli_repo_smoke_test; do
  TEST_PATH="$BIN_DIR/${test_exe}.exe"
  if [[ -f "$TEST_PATH" ]]; then
    echo ""
    echo "[TEST] Running: $test_exe"
    if "$TEST_PATH"; then
      echo "[PASS] $test_exe"
      add_junit_case "$test_exe" pass
    else
      exit_code=$?
      echo "[FAIL] $test_exe (exit $exit_code)"
      FAILED=$((FAILED + 1))
      add_junit_case "$test_exe" fail "exit $exit_code"
    fi
  else
    echo "[SKIP] $test_exe (not found)"
    add_junit_case "$test_exe" skip "binary not found"
  fi
done

echo ""
write_junit_report
if [[ $FAILED -eq 0 ]]; then
  echo "=== All tests passed ==="
else
  echo "=== $FAILED test(s) failed ==="
  exit 1
fi
