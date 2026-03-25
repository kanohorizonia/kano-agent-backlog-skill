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
export KOB_CPP_ROOT="${KOB_CPP_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

PRESET="${1:-windows-ninja-msvc}"
WITH_E2E="${2:-}"

echo "=== kano-backlog test runner ==="
echo "  KOB_CPP_ROOT: $KOB_CPP_ROOT"
echo "  PRESET: $PRESET"

# Build directory from preset
BUILD_DIR="$KOB_CPP_ROOT/build/_intermediate/$PRESET"

# Determine binary directory (Debug is default for this repo)
BIN_DIR="$KOB_CPP_ROOT/out/bin/debug"

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

for test_exe in backlog_core_smoke_test workitem_ops_smoke_test cli_repo_smoke_test; do
  TEST_PATH="$BIN_DIR/${test_exe}.exe"
  if [[ -f "$TEST_PATH" ]]; then
    echo ""
    echo "[TEST] Running: $test_exe"
    if "$TEST_PATH"; then
      echo "[PASS] $test_exe"
    else
      echo "[FAIL] $test_exe (exit $?)"
      FAILED=$((FAILED + 1))
    fi
  else
    echo "[SKIP] $test_exe (not found)"
  fi
done

echo ""
if [[ $FAILED -eq 0 ]]; then
  echo "=== All tests passed ==="
else
  echo "=== $FAILED test(s) failed ==="
  exit 1
fi
