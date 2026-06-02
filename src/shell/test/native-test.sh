#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

DEFAULT_PRESET=""
DEFAULT_CONFIG="Debug"

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*) DEFAULT_PRESET="windows-ninja-msvc" ;;
  Linux) DEFAULT_PRESET="linux-ninja-clang" ;;
  Darwin) DEFAULT_PRESET="macos-ninja-clang-x64" ;;
  *)
    echo "Unsupported platform for native test runner." >&2
    exit 1
    ;;
esac

PRESET="${1:-$DEFAULT_PRESET}"
CONFIG="${2:-$DEFAULT_CONFIG}"
shift $(( $# > 0 ? 1 : 0 )) || true
shift $(( $# > 0 ? 1 : 0 )) || true

ctest --test-dir "$SKILL_ROOT/src/cpp/out/obj/$PRESET" -C "$CONFIG" --output-on-failure "$@"
