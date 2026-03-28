#!/usr/bin/env bash
# =============================================================================
# Windows MSVC Coverage Build
# =============================================================================
# Builds with /PROFILE flag for coverage instrumentation.
# Run: ninja-msvc-coverage-run.sh to execute tests with coverage collection
# Run: ninja-msvc-coverage-report.sh to generate report
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="${KOB_CPP_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
source "$SCRIPT_DIR/../common/windows_preset_build.sh"

echo "[coverage-build] KOB_CPP_ROOT: $KOB_CPP_ROOT"
echo "[coverage-build] Config: Debug (coverage requires debug symbols)"

echo "[coverage-build] Running coverage build..."
kabsd_run_windows_preset "windows-ninja-msvc-coverage" "windows-ninja-msvc-coverage-debug" "x64"

echo "[coverage-build] Done. Build dir: $KOB_CPP_ROOT/out/obj/windows-ninja-msvc-coverage"
