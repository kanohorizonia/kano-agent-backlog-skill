#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOG_CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

export KANO_CPP_INFRA_CPP_ROOT="${KANO_CPP_INFRA_CPP_ROOT:-$KOG_CPP_ROOT}"
source "$KOG_CPP_ROOT/shared/infra/scripts/lib/native_tool.sh"
kano_cpp_infra_watchdog_enter "$0" "$@"

source "$SCRIPT_DIR/../common/windows_preset_build.sh"

kog_run_windows_preset "windows-ninja-msvc" "windows-ninja-msvc-release" "x64"
