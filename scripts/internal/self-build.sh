#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
export KANO_CPP_ROOT="$SKILL_ROOT/src/cpp"

# Delegate to expert skill for canonical build infrastructure (prerequisites, vcvarsall, etc.)
export KOG_EXPERT_SKILL_ROOT="C:/Users/dorgon.chang/.agents/skills/kano/kano-cpp-expert-skill"
export KOG_CPP_ROOT="$KANO_CPP_ROOT"

usage() {
  cat <<'EOF'
Usage: self-build.sh [debug|release]

Build the active native kob surface for this demo repo using src/cpp/scripts.
EOF
}

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  -h|--help) usage; exit 0 ;;
  *) echo "Unsupported mode: $MODE" >&2; usage >&2; exit 1 ;;
esac

CPP_SCRIPTS_DIR="$SKILL_ROOT/src/cpp/scripts"

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$CPP_SCRIPTS_DIR/windows/ninja-msvc-release.sh"
    fi
    exec bash "$CPP_SCRIPTS_DIR/windows/ninja-msvc-debug.sh"
    ;;
  Linux)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$CPP_SCRIPTS_DIR/linux/ninja-gcc-release.sh"
    fi
    exec bash "$CPP_SCRIPTS_DIR/linux/ninja-gcc-debug.sh"
    ;;
  Darwin)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$CPP_SCRIPTS_DIR/macos/ninja-clang-x64-release.sh"
    fi
    exec bash "$CPP_SCRIPTS_DIR/macos/ninja-clang-x64-debug.sh"
    ;;
  *)
    echo "Unsupported platform for self-build." >&2
    exit 1
    ;;
esac
