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

KOB_WINDOWS_PS_HELPER="$SCRIPT_DIR/../common/windows_preset_helper.ps1"

kob_powershell_bin() {
  local candidate
  for candidate in powershell powershell.exe pwsh pwsh.exe; do
    if command -v "$candidate" >/dev/null 2>&1; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

echo "[coverage-build] KOB_CPP_ROOT: $KOB_CPP_ROOT"
echo "[coverage-build] Config: Debug (coverage requires debug symbols)"

COVERAGE_BUILD_DIR="${KOB_COVERAGE_BUILD_DIR:-build/_intermediate/windows-ninja-msvc-coverage}"

PowerShellBin="$(kob_powershell_bin)" || {
  echo "PowerShell is required." >&2
  exit 1
}

# Source build metadata for version info
if [[ -f "$SCRIPT_DIR/../common/build_metadata.sh" ]]; then
  source "$SCRIPT_DIR/../common/build_metadata.sh"
fi

echo "[coverage-build] Running coverage build..."
"$PowerShellBin" -NoProfile -ExecutionPolicy Bypass -File "$KOB_WINDOWS_PS_HELPER" \
  -Action run-coverage-build \
  -Root "$KOB_CPP_ROOT" \
  -CoverageBuildDir "$COVERAGE_BUILD_DIR" \
  -Config Debug \
  -Generator "Ninja Multi-Config" \
  -Arch x64

echo "[coverage-build] Done. Binary: $KOB_CPP_ROOT/$COVERAGE_BUILD_DIR/out/bin/debug/"
