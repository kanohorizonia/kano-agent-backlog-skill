#!/usr/bin/env bash
# =============================================================================
# ninja-clang-x64-debug.sh — macOS x64 (Intel) Clang Debug build
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKLOG_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
SRC_CPP="$BACKLOG_ROOT/src/cpp"

source "$SRC_CPP/scripts/common/env.sh"
source "$SRC_CPP/scripts/macos/prerequisite_macos.sh"

: "${KOG_CONFIG:=Debug}"
: "${KOG_PRESET_NAME:=macos-ninja-clang-x64}"

BUILD_DIR="$SRC_CPP/out/obj/$KOG_PRESET_NAME"

cmake --preset "$KOG_PRESET_NAME" -B "$BUILD_DIR" -S "$SRC_CPP"
cmake --build "$BUILD_DIR" --config "$KOG_CONFIG" --parallel
