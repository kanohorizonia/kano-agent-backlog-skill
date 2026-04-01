#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODE="${1:-debug}"

case "$MODE" in
  debug|release) ;;
  -h|--help)
    echo "Usage: self-rebuild.sh [debug|release]"
    exit 0
    ;;
  *)
    echo "Unsupported mode: $MODE" >&2
    exit 1
    ;;
esac

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    rm -rf "$SKILL_ROOT/src/cpp/out/obj/windows-ninja-msvc"
    if [[ "$MODE" == "release" ]]; then
      rm -rf "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/release"
    else
      rm -rf "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/debug"
    fi
    ;;
  Linux)
    if [[ "$MODE" == "release" ]]; then
      rm -rf "$SKILL_ROOT/src/cpp/out/obj/linux-ninja-gcc" "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/release"
    else
      rm -rf "$SKILL_ROOT/src/cpp/out/obj/linux-ninja-gcc" "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/debug"
    fi
    ;;
  Darwin)
    if [[ "$MODE" == "release" ]]; then
      rm -rf "$SKILL_ROOT/src/cpp/out/obj/macos-ninja-clang" "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/release"
    else
      rm -rf "$SKILL_ROOT/src/cpp/out/obj/macos-ninja-clang" "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/debug"
    fi
    ;;
  *)
    echo "Unsupported platform for self-rebuild." >&2
    exit 1
    ;;
esac

exec bash "$SCRIPT_DIR/self-build.sh" "$MODE"
