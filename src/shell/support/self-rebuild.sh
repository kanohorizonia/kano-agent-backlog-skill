#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
MODE="${1:-release}"

case "$MODE" in
  debug|release) ;;
  -h|--help)
    echo "Usage: self-rebuild.sh [release|debug]"
    echo "Default: release"
    exit 0
    ;;
  *)
    echo "Unsupported mode: $MODE" >&2
    echo "Usage: self-rebuild.sh [release|debug]" >&2
    exit 1
    ;;
esac

# Clean build artifacts for the canonical preset on this platform, then rebuild.
# IMPORTANT: preset names here MUST match self-build.sh exactly.
case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    # Windows: canonical preset is windows-ninja-msvc (with msbuild fallback).
    # Clean both obj (configure cache) and the specific config's bin output.
    rm -rf "$SKILL_ROOT/src/cpp/out/obj/windows-ninja-msvc"
    rm -rf "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/$MODE"
    # Also clean msbuild fallback paths to avoid stale binaries confusing resolution.
    rm -rf "$SKILL_ROOT/src/cpp/out/obj/windows-msbuild"
    rm -rf "$SKILL_ROOT/src/cpp/out/bin/windows-msbuild/$MODE"
    ;;
  Linux)
    # Linux: canonical preset is linux-ninja-clang (same as self-build.sh).
    rm -rf "$SKILL_ROOT/src/cpp/out/obj/linux-ninja-clang"
    rm -rf "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-clang/$MODE"
    ;;
  Darwin)
    # macOS: canonical preset is macos-ninja-clang (same as self-build.sh).
    rm -rf "$SKILL_ROOT/src/cpp/out/obj/macos-ninja-clang"
    rm -rf "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/$MODE"
    ;;
  *)
    echo "Unsupported platform for self-rebuild." >&2
    exit 1
    ;;
esac

exec bash "$SCRIPT_DIR/self-build.sh" "$MODE"
