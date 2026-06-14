#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# ---------------------------------------------------------------------------
# Detect install mode
# ---------------------------------------------------------------------------
_mode="unknown"
if [[ -f "$SKILL_ROOT/VERSION" && \
      -f "$SKILL_ROOT/src/cpp/CMakeLists.txt" && \
      -f "$SKILL_ROOT/pixi.toml" ]]; then
  _mode="developer-checkout"
fi

# ---------------------------------------------------------------------------
# Detect native binary (release-first)
# ---------------------------------------------------------------------------
_exe_suffix=""
case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*) _exe_suffix=".exe" ;;
esac

_active_binary=""
_active_build_config="unknown"

if [[ -n "${KANO_BACKLOG_BINARY:-}" && -f "${KANO_BACKLOG_BINARY}" ]]; then
  _active_binary="$KANO_BACKLOG_BINARY"
  # Try to infer config from path
  case "$_active_binary" in
    */release/*) _active_build_config="release" ;;
    */debug/*)   _active_build_config="debug" ;;
    *)           _active_build_config="unknown" ;;
  esac
else
  case "$(uname -s 2>/dev/null || printf 'unknown')" in
    MINGW*|MSYS*|CYGWIN*)
      _candidates=(
        "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/release/kano-backlog.exe"
        "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog.exe"
        "$SKILL_ROOT/src/cpp/out/bin/windows-msbuild/release/kano-backlog.exe"
        "$SKILL_ROOT/src/cpp/out/bin/windows-msbuild/debug/kano-backlog.exe"
      )
      ;;
    Linux)
      _candidates=(
        "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-clang/release/kano-backlog"
        "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-clang/debug/kano-backlog"
        "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/release/kano-backlog"
        "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/debug/kano-backlog"
      )
      ;;
    Darwin)
      _candidates=(
        "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/release/kano-backlog"
        "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/debug/kano-backlog"
      )
      ;;
    *)
      _candidates=()
      ;;
  esac

  for _c in "${_candidates[@]+"${_candidates[@]}"}"; do
    if [[ -f "$_c" ]]; then
      _active_binary="$_c"
      case "$_c" in
        */release/*) _active_build_config="release" ;;
        */debug/*)   _active_build_config="debug" ;;
        *)           _active_build_config="unknown" ;;
      esac
      break
    fi
  done

  # Legacy flat-layout fallback
  if [[ -z "$_active_binary" && -f "$SKILL_ROOT/src/cpp/out/bin/kano-backlog$_exe_suffix" ]]; then
    _active_binary="$SKILL_ROOT/src/cpp/out/bin/kano-backlog$_exe_suffix"
    _active_build_config="unknown"
  fi
fi

_native_exists="false"
if [[ -n "$_active_binary" ]]; then
  _native_exists="true"
fi

# ---------------------------------------------------------------------------
# Output key=value pairs
# ---------------------------------------------------------------------------
printf 'skill_id=kano-agent-backlog-skill\n'
printf 'cli=kob\n'
printf 'repo_root=%s\n'          "$SKILL_ROOT"
printf 'cpp_root=%s\n'           "$SKILL_ROOT/src/cpp"
printf 'version_file=%s\n'       "$SKILL_ROOT/VERSION"
printf 'build_output_root=%s\n'  "$SKILL_ROOT/src/cpp/out/bin"
printf 'launcher=%s\n'           "$SKILL_ROOT/scripts/kob"
printf 'default_build_config=release\n'
printf 'active_build_config=%s\n'  "$_active_build_config"
printf 'active_binary=%s\n'        "${_active_binary:-}"
printf 'native_binary_exists=%s\n' "$_native_exists"
printf 'mode=%s\n'                 "$_mode"
printf 'python_runtime=false\n'
