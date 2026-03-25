#!/usr/bin/env bash
# =============================================================================
# Windows MSVC Coverage - Run Tests with Collection
# =============================================================================
# Uses Microsoft.CodeCoverage.Console to collect coverage data from test run.
# Output: .coverage binary file (can be converted with ninja-msvc-coverage-report.sh)
#
# Usage:
#   bash ninja-msvc-coverage-run.sh
#
# Environment:
#   KOB_COVERAGE_ROOT  - Output directory for coverage files
#                       (default: $KOB_CPP_ROOT/out/coverage)
#   KOB_COVERAGE_BUILD_DIR - Build dir for coverage binary
#                       (default: build/_intermediate/windows-ninja-msvc-coverage)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="${KOB_CPP_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
KOB_COVERAGE_ROOT="${KOB_COVERAGE_ROOT:-${KOB_CPP_ROOT}/out/coverage}"
COVERAGE_BUILD_DIR="${KOB_COVERAGE_BUILD_DIR:-build/_intermediate/windows-ninja-msvc-coverage}"

# Detect CodeCoverage.Console
KOB_CODE_COVERAGE_CONSOLE="${KOB_CODE_COVERAGE_CONSOLE:-}"
if [[ -z "$KOB_CODE_COVERAGE_CONSOLE" ]]; then
  for path in \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe"
  do
    if [[ -x "$path" ]]; then
      KOB_CODE_COVERAGE_CONSOLE="$path"
      break
    fi
  done
fi

if [[ -z "$KOB_CODE_COVERAGE_CONSOLE" || ! -x "$KOB_CODE_COVERAGE_CONSOLE" ]]; then
  echo "[ERROR] Microsoft.CodeCoverage.Console not found." >&2
  echo "[ERROR] Set KOB_CODE_COVERAGE_CONSOLE to the path." >&2
  exit 1
fi

# Binary paths
BINARY_DIR="$KOB_CPP_ROOT/$COVERAGE_BUILD_DIR/out/bin/debug"

# Find test binaries
TEST_BINARIES=()
for binary in backlog_core_smoke_test.exe workitem_ops_smoke_test.exe cli_repo_smoke_test.exe; do
  if [[ -f "$BINARY_DIR/$binary" ]]; then
    TEST_BINARIES+=("$BINARY_DIR/$binary")
  fi
done

if [[ ${#TEST_BINARIES[@]} -eq 0 ]]; then
  echo "[ERROR] No test binaries found in: $BINARY_DIR" >&2
  echo "[ERROR] Run ninja-msvc-coverage-build.sh first." >&2
  exit 1
fi

mkdir -p "$KOB_COVERAGE_ROOT"

# Run each test binary with coverage collection
for TEST_BINARY in "${TEST_BINARIES[@]}"; do
  BINARY_NAME="$(basename "$TEST_BINARY" .exe)"
  COVERAGE_OUTPUT="$KOB_COVERAGE_ROOT/${BINARY_NAME}.coverage"

  echo "[coverage-run] Binary: $TEST_BINARY"
  echo "[coverage-run] Output: $COVERAGE_OUTPUT"

  "$KOB_CODE_COVERAGE_CONSOLE" collect "$TEST_BINARY" \
    -o "$COVERAGE_OUTPUT" \
    -f coverage \
    2>&1 || {
    echo "[ERROR] Coverage collection failed for $TEST_BINARY" >&2
    exit 1
  }

  echo "[coverage-run] Done: $COVERAGE_OUTPUT"
done

echo ""
echo "[coverage-run] All coverage files:"
for f in "$KOB_COVERAGE_ROOT"/*.coverage; do
  if [[ -f "$f" ]]; then
    echo "  $f"
  fi
done
echo "[coverage-run] Run ninja-msvc-coverage-report.sh to generate reports."
