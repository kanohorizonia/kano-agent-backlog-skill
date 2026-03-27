#!/usr/bin/env bash
# =============================================================================
# ninja-gcc-release.sh — Linux GCC Release build
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKLOG_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
SRC_CPP="$BACKLOG_ROOT/src/cpp"

source "$SRC_CPP/scripts/common/env.sh"
source "$SRC_CPP/scripts/linux/prerequisite_linux.sh"

: "${KOG_CONFIG:=Release}"
: "${KOG_PRESET_NAME:=linux-ninja-gcc}"

BUILD_DIR="$SRC_CPP/out/obj/$KOG_PRESET_NAME"

cmake --preset "$KOG_PRESET_NAME" -B "$BUILD_DIR" -S "$SRC_CPP"
cmake --build "$BUILD_DIR" --config "$KOG_CONFIG" --parallel
