#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

usage() {
  cat <<'EOF'
Usage: self-build.sh [debug|release]

Build the active native kob surface for this demo repo.
EOF
}

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  -h|--help) usage; exit 0 ;;
  *) echo "Unsupported mode: $MODE" >&2; usage >&2; exit 1 ;;
esac

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$SKILL_ROOT/src/cpp/build/script/windows/build_windows_ninja_msvc_release.sh"
    fi
    exec bash "$SKILL_ROOT/src/cpp/build/script/windows/build_windows_ninja_msvc_debug.sh"
    ;;
  Linux)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$SKILL_ROOT/src/cpp/build/script/linux/build_linux_ninja_gcc_release.sh"
    fi
    exec bash "$SKILL_ROOT/src/cpp/build/script/linux/build_linux_ninja_gcc_debug.sh"
    ;;
  Darwin)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$SKILL_ROOT/src/cpp/build/script/macos/build_macos_ninja_clang_release.sh"
    fi
    exec bash "$SKILL_ROOT/src/cpp/build/script/macos/build_macos_ninja_clang_debug.sh"
    ;;
  *)
    echo "Unsupported platform for self-build." >&2
    exit 1
    ;;
esac
