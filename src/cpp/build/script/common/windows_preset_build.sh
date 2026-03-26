#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_metadata.sh"

if [[ -z "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  echo "KOB_CPP_ROOT is not set." >&2
  exit 1
fi

KABSD_WINDOWS_PS_HELPER="$SCRIPT_DIR/windows_preset_helper.ps1"

kabsd_powershell_bin() {
  local candidate
  for candidate in powershell powershell.exe pwsh pwsh.exe; do
    if command -v "$candidate" >/dev/null 2>&1; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

kabsd_run_windows_build() {
  local build_dir="$1"
  local config="$2"
  local generator="${3:-Ninja}"
  local arch="${4:-x64}"
  local powershell_bin=""

  powershell_bin="$(kabsd_powershell_bin)" || {
    echo "PowerShell is required." >&2
    return 127
  }

  kob_apply_self_build_config
  kob_collect_build_metadata

  "$powershell_bin" -NoProfile -ExecutionPolicy Bypass -File "$KABSD_WINDOWS_PS_HELPER" \
    -Action run-build \
    -Root "$(kob_cpp_root)" \
    -BuildDir "$build_dir" \
    -Config "$config" \
    -Generator "$generator" \
    -Arch "$arch"
}
