#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
EXPECTED_VERSION="$(tr -d '\r\n' < "$SKILL_ROOT/VERSION")"

find_native_bin() {
  local exe_suffix=""
  local -a presets=(
    windows-ninja-msvc
    windows-ninja-clang
    windows-msbuild
    linux-ninja-clang
    linux-ninja-gcc
    macos-ninja-clang
    macos-ninja-clang-x64
    macos-ninja-clang-arm64
  )
  local -a configs=(release debug relwithdebinfo minsizerel)
  local preset
  local config
  local candidate

  case "$(uname -s 2>/dev/null || printf 'unknown')" in
    MINGW*|MSYS*|CYGWIN*) exe_suffix=".exe" ;;
  esac

  for preset in "${presets[@]}"; do
    for config in "${configs[@]}"; do
      candidate="$SKILL_ROOT/src/cpp/out/bin/$preset/$config/kano-backlog$exe_suffix"
      if [[ -f "$candidate" ]]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  done

  return 1
}

NATIVE_BIN="$(find_native_bin)"
VERSION_OUTPUT="$("$NATIVE_BIN" --version 2>&1 || true)"
if ! printf '%s\n' "$VERSION_OUTPUT" | grep -q "$EXPECTED_VERSION"; then
  echo "Native binary version does not match VERSION $EXPECTED_VERSION" >&2
  echo "$VERSION_OUTPUT" >&2
  exit 1
fi

if grep -Eq "PYTHON_BIN|kano_backlog_cli\\.cli:main|KANO_BACKLOG_ALLOW_PYTHON_FALLBACK" "$SKILL_ROOT/src/shell/core/kano-backlog"; then
  echo "Launcher still contains Python fallback code." >&2
  exit 1
fi

if [[ -e "$SKILL_ROOT/src/python" ]]; then
  echo "Removed Python runtime source directory is present again: src/python" >&2
  exit 1
fi

if [[ -e "$SKILL_ROOT/tests" || -e "$SKILL_ROOT/test_vcs.py" ]]; then
  echo "Removed pytest oracle files are present again." >&2
  exit 1
fi

remaining_py="$(
  find "$SKILL_ROOT" -type f \( -name '*.py' -o -name '*.pyi' \) \
    ! -path "$SKILL_ROOT/src/cpp/out/*" \
    ! -path "$SKILL_ROOT/src/shell/release/post_release_verify.py" \
    ! -path "$SKILL_ROOT/.git/*" \
    ! -path "$SKILL_ROOT/.kano/*" \
    ! -path "$SKILL_ROOT/.pixi/*" \
    2>/dev/null || true
)"
if [[ -n "$remaining_py" ]]; then
  echo "Python source or typing stub files remain outside the bounded release-only verifier:" >&2
  printf '%s\n' "$remaining_py" >&2
  exit 1
fi

if grep -Eq '^[[:space:]]*(python|pip)[[:space:]]*=' "$SKILL_ROOT/pixi.toml" ||
   grep -Eq '^\[pypi-dependencies\]' "$SKILL_ROOT/pixi.toml" ||
   grep -Eq 'kano-agent-backlog-skill[[:space:]]*=[[:space:]]*\{[[:space:]]*path[[:space:]]*=' "$SKILL_ROOT/pixi.toml"; then
  echo "pixi default environment still declares Python runtime/package dependencies." >&2
  exit 1
fi

if grep -Eq '^[[:space:]]+- pypi:|conda: .*/(python|pip|setuptools|wheel)-' "$SKILL_ROOT/pixi.lock"; then
  echo "pixi lock still contains Python runtime/package records." >&2
  exit 1
fi

windows_error_refs="$(
  grep -RInE "Set(ErrorMode|ThreadErrorMode)|_CrtSetReportMode|_set_abort_behavior|_set_invalid_parameter_handler" \
    "$SKILL_ROOT/src/cpp/code" "$SKILL_ROOT/src/cpp/tests" 2>/dev/null |
    grep -v "noninteractive_errors.hpp" || true
)"
if [[ -n "$windows_error_refs" ]]; then
  echo "Windows assert/error-dialog suppression must stay centralized in noninteractive_errors.hpp:" >&2
  printf '%s\n' "$windows_error_refs" >&2
  exit 1
fi

"$SKILL_ROOT/scripts/kob" --version >/dev/null
"$SKILL_ROOT/scripts/kob" doctor >/dev/null

# ---------------------------------------------------------------------------
# Wrapper pollution gate: reject wrong-product KOG launchers in backlog scripts.
# KOG launchers (kog, kog.bat, kano-git, kano-git.bat) belong exclusively to
# kano-git-master-skill and must never appear in kano-agent-backlog-skill.
# ---------------------------------------------------------------------------
_BAD_LAUNCHERS=(kog kog.bat kano-git kano-git.bat)
for _launcher in "${_BAD_LAUNCHERS[@]}"; do
  if [[ -e "$SKILL_ROOT/scripts/$_launcher" ]]; then
    echo "Wrong-product launcher found in kano-agent-backlog-skill/scripts: $_launcher" >&2
    echo "KOG launchers belong to kano-git-master-skill, not backlog skill." >&2
    exit 1
  fi
done

echo "Native runtime gate passed."
