#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
export KANO_CPP_ROOT="$SKILL_ROOT/src/cpp"
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
INFRA_SCRIPTS_DIR="$KANO_CPP_ROOT/shared/infra/scripts"

detect_real_windows_ninja() {
  local candidate
  for candidate in \
    "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
  do
    if [[ -x "$candidate" || -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    source "$CPP_SCRIPTS_DIR/common/windows_preset_build.sh"
    REAL_NINJA="$(detect_real_windows_ninja || true)"
    if [[ -n "$REAL_NINJA" ]]; then
      export KOG_CMAKE_CACHE_ARGS_JSON="{\"CMAKE_MAKE_PROGRAM\":\"$REAL_NINJA\",\"KB_BUILD_TESTS\":\"ON\"}"
      if [[ "$MODE" == "release" ]]; then
        kog_run_windows_preset "windows-ninja-msvc" "windows-ninja-msvc-release" "x64"
        exit $?
      fi
      kog_run_windows_preset "windows-ninja-msvc" "windows-ninja-msvc-debug" "x64"
      exit $?
    fi

    # Fall back to the Visual Studio generator when no real Ninja binary is available.
    export KOG_CMAKE_CACHE_ARGS_JSON='{"KB_BUILD_TESTS":"ON"}'
    if [[ "$MODE" == "release" ]]; then
      kog_run_windows_preset "windows-msbuild" "windows-msbuild-release" "x64"
      exit $?
    fi
    kog_run_windows_preset "windows-msbuild" "windows-msbuild-debug" "x64"
    exit $?
    ;;
  Linux)
    export INF_CMAKE_CACHE_ARGS_JSON='{"KB_BUILD_TESTS":"ON"}'
    if [[ "$MODE" == "release" ]]; then
      exec bash "$INFRA_SCRIPTS_DIR/platform/linux/native-build.sh" "linux-ninja-clang" "linux-ninja-clang-release" "KB"
    fi
    exec bash "$INFRA_SCRIPTS_DIR/platform/linux/native-build.sh" "linux-ninja-clang" "linux-ninja-clang-debug" "KB"
    ;;
  Darwin)
    export INF_CMAKE_CACHE_ARGS_JSON='{"KB_BUILD_TESTS":"ON"}'
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
