#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
export KANO_CPP_ROOT="$SKILL_ROOT/src/cpp"

usage() {
  cat <<'EOF'
Usage: self-build.sh [debug|release]

Build the active native kob surface for this demo repo using shared/infra directly.
EOF
}

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  -h|--help) usage; exit 0 ;;
  *) echo "Unsupported mode: $MODE" >&2; usage >&2; exit 1 ;;
esac

INFRA_DIR="$SKILL_ROOT/src/cpp/shared/infra/scripts"

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$INFRA_DIR/windows/ninja-msvc-release.sh"
    fi
    exec bash "$INFRA_DIR/windows/ninja-msvc-debug.sh"
    ;;
  Linux)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$INFRA_DIR/linux/native-build.sh" linux-ninja-gcc linux-ninja-gcc-release KANO
    fi
    exec bash "$INFRA_DIR/linux/native-build.sh" linux-ninja-gcc linux-ninja-gcc-debug KANO
    ;;
  Darwin)
    if [[ "$MODE" == "release" ]]; then
      exec bash "$INFRA_DIR/macos/native-build.sh" macos-ninja-clang macos-ninja-clang-release KOG
    fi
    exec bash "$INFRA_DIR/macos/native-build.sh" macos-ninja-clang macos-ninja-clang-debug KOG
    ;;
  *)
    echo "Unsupported platform for self-build." >&2
    exit 1
    ;;
esac
