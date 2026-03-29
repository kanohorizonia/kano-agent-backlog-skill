#!/usr/bin/env bash
# =============================================================================
# ninja-clang-x64-release.sh — macOS x64 (Intel) Clang Release build
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOG_CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$SCRIPT_DIR/../common/unix_preset_build.sh"
source "$SCRIPT_DIR/prerequisite_macos.sh"

kabld_run_unix_preset "macos-ninja-clang-x64" "macos-ninja-clang-x64-release"
