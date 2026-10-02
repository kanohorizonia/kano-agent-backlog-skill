#!/usr/bin/env bash
# =============================================================================
# ninja-clang-x64-debug.sh — macOS x64 (Intel) Clang Debug build
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOG_CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

export KANO_CPP_INFRA_CPP_ROOT="${KANO_CPP_INFRA_CPP_ROOT:-$KOG_CPP_ROOT}"
source "$KOG_CPP_ROOT/shared/infra/scripts/lib/native_tool.sh"
kano_cpp_infra_watchdog_enter "$0" "$@"

source "$SCRIPT_DIR/../common/unix_preset_build.sh"
source "$SCRIPT_DIR/prerequisite_macos.sh"

kabld_run_unix_preset "macos-ninja-clang-x64" "macos-ninja-clang-x64-debug"
