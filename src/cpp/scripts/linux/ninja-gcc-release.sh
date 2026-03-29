#!/usr/bin/env bash
# =============================================================================
# ninja-gcc-release.sh — Linux GCC Release build
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOG_CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$SCRIPT_DIR/../common/unix_preset_build.sh"
source "$SCRIPT_DIR/prerequisite_linux.sh"

kabld_run_unix_preset "linux-ninja-gcc" "linux-ninja-gcc-release"
