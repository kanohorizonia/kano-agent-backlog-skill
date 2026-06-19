#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

export CI="${CI:-true}"
export KANO_BACKLOG_NONINTERACTIVE=1
export KANO_TEST_NONINTERACTIVE=1
export GIT_TERMINAL_PROMPT=0

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
LANE="${3:-default}"
shift $(( $# > 0 ? 1 : 0 )) || true
shift $(( $# > 0 ? 1 : 0 )) || true
shift $(( $# > 0 ? 1 : 0 )) || true

case "$LANE" in
  quick)
    export KANO_NATIVE_TEST_TIMEOUT_SECONDS="${KANO_NATIVE_TEST_TIMEOUT_SECONDS:-30}"
    ;;
  *)
    export KANO_NATIVE_TEST_TIMEOUT_SECONDS="${KANO_NATIVE_TEST_TIMEOUT_SECONDS:-120}"
    ;;
esac

prepare_windows_ctest_drive

KANO_CTEST_OUTPUT_SIZE_PASSED="${KANO_CTEST_OUTPUT_SIZE_PASSED:-262144}"
KANO_CTEST_OUTPUT_SIZE_FAILED="${KANO_CTEST_OUTPUT_SIZE_FAILED:-1048576}"

run_ctest_with_junit() {
  local lane="${1:-default}"
  shift $(( $# > 0 ? 1 : 0 )) || true
  local build_dir="$SKILL_ROOT/src/cpp/out/obj/$PRESET"
  local test_count
  local ctest_args=(
    --test-dir "$build_dir"
    -C "$CONFIG"
    --output-on-failure
    --timeout "$KANO_NATIVE_TEST_TIMEOUT_SECONDS"
    --test-output-size-passed "$KANO_CTEST_OUTPUT_SIZE_PASSED"
    --test-output-size-failed "$KANO_CTEST_OUTPUT_SIZE_FAILED"
  )

  if [[ ! -d "$build_dir" ]]; then
    echo "Missing CTest build directory: $build_dir" >&2
    return 1
  fi

  test_count="$(
    ctest --test-dir "$build_dir" -C "$CONFIG" -N 2>/dev/null |
      awk '/^  Test #[0-9]+:/ { count++ } END { print count + 0 }'
  )"
  if [[ "$test_count" == "0" ]]; then
    echo "No CTest tests are registered for preset=$PRESET config=$CONFIG. Was KB_BUILD_TESTS enabled?" >&2
    return 1
  fi

  if [[ "$lane" == "quick" ]]; then
    ctest_args+=(--stop-on-failure -LE long)
  fi

  echo "[INFO] CTest lane: $lane"
  echo "[INFO] CTest timeout seconds: $KANO_NATIVE_TEST_TIMEOUT_SECONDS"
  echo "[INFO] CTest output size passed: $KANO_CTEST_OUTPUT_SIZE_PASSED"
  echo "[INFO] CTest output size failed: $KANO_CTEST_OUTPUT_SIZE_FAILED"

  if [[ -n "${KANO_TEST_XML:-}" ]]; then
    case "$KANO_TEST_XML" in
      /*|[A-Za-z]:/*|[A-Za-z]:\\*) ;;
      *) KANO_TEST_XML="$SKILL_ROOT/$KANO_TEST_XML" ;;
    esac
    mkdir -p "$(dirname "$KANO_TEST_XML")"
    ctest_args+=(--output-junit "$KANO_TEST_XML")
    set +e
    ctest "${ctest_args[@]}" "$@"
    local ctest_rc=$?
    set -e
    if [[ $ctest_rc -ne 0 ]]; then
      echo "[ERROR] CTest failed with exit code $ctest_rc" >&2
      echo "[ERROR] JUnit report path: $KANO_TEST_XML" >&2
      return "$ctest_rc"
    fi
    if [[ ! -f "$KANO_TEST_XML" ]]; then
      echo "CTest completed but did not write JUnit report: $KANO_TEST_XML" >&2
      return 1
    fi
    echo "[INFO] JUnit report written: $KANO_TEST_XML"
    return 0
  fi

  ctest "${ctest_args[@]}" "$@"
}

run_ctest_with_junit "$LANE" "$@"
