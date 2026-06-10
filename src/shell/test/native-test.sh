#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

export CI="${CI:-true}"
export KANO_BACKLOG_NONINTERACTIVE=1
export KANO_TEST_NONINTERACTIVE=1
export GIT_TERMINAL_PROMPT=0
export KANO_NATIVE_TEST_TIMEOUT_SECONDS="${KANO_NATIVE_TEST_TIMEOUT_SECONDS:-120}"

cleanup_subst_drive() {
  local drive="$1"
  if [[ -z "$drive" ]]; then
    return 0
  fi
  if command -v cmd.exe >/dev/null 2>&1; then
    cmd.exe /d /c "subst ${drive} /d" >/dev/null 2>&1 || true
  fi
}

prepare_windows_ctest_drive() {
  if [[ "$PRESET" != windows-* ]]; then
    return 0
  fi
  if ! command -v cmd.exe >/dev/null 2>&1; then
    return 0
  fi

  local drive="Z:"
  local root_win
  if command -v cygpath >/dev/null 2>&1; then
    root_win="$(cygpath -w "$SKILL_ROOT/src/cpp")"
  else
    root_win="$SKILL_ROOT\\src\\cpp"
  fi

  cleanup_subst_drive "$drive"
  cmd.exe /d /c "subst ${drive} \"${root_win}\"" >/dev/null
  trap 'cleanup_subst_drive "Z:"' EXIT
}

run_windows_native_smoke_tests() {
  local config_dir
  config_dir="$(printf '%s' "$CONFIG" | tr '[:upper:]' '[:lower:]')"
  local bin_root="$SKILL_ROOT/src/cpp/out/bin/$PRESET/$config_dir"
  local exe

  for exe in backlog_core_smoke_test.exe workitem_ops_smoke_test.exe cli_repo_smoke_test.exe; do
    if [[ ! -f "$bin_root/$exe" ]]; then
      echo "Missing native smoke test executable: $bin_root/$exe" >&2
      return 1
    fi
    if command -v timeout >/dev/null 2>&1; then
      timeout "${KANO_NATIVE_TEST_TIMEOUT_SECONDS}s" "$bin_root/$exe"
    else
      "$bin_root/$exe"
    fi
  done
}

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

prepare_windows_ctest_drive

if [[ "$PRESET" == windows-* ]]; then
  run_windows_native_smoke_tests
  exit $?
fi

ctest --test-dir "$SKILL_ROOT/src/cpp/out/obj/$PRESET" -C "$CONFIG" --output-on-failure "$@"
