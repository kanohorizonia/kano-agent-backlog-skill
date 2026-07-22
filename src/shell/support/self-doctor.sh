#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

_pass=0
_fail=0
_warn=0

_ok()   { printf '[OK]   %s\n' "$1"; (( _pass++ )) || true; }
_fail() { printf '[FAIL] %s\n' "$1" >&2; (( _fail++ )) || true; }
_warn() { printf '[WARN] %s\n' "$1"; (( _warn++ )) || true; }

echo "kob self doctor — kano-agent-backlog-skill"
echo "==========================================="
echo "repo_root: $SKILL_ROOT"
echo ""

# ---------------------------------------------------------------------------
# 1. Repo structure checks
# ---------------------------------------------------------------------------
echo "--- Repo structure ---"

if [[ -f "$SKILL_ROOT/VERSION" ]]; then
  _ok "VERSION exists ($(tr -d '\r\n' < "$SKILL_ROOT/VERSION"))"
else
  _fail "VERSION not found"
fi

if [[ -f "$SKILL_ROOT/pixi.toml" ]]; then
  _ok "pixi.toml exists"
else
  _fail "pixi.toml not found"
fi

if [[ -f "$SKILL_ROOT/src/cpp/CMakeLists.txt" ]]; then
  _ok "src/cpp/CMakeLists.txt exists (developer checkout)"
else
  _fail "src/cpp/CMakeLists.txt not found — self build/rebuild requires developer checkout"
fi

# ---------------------------------------------------------------------------
# 2. Shell syntax checks
# ---------------------------------------------------------------------------
echo ""
echo "--- Shell syntax ---"

for _f in \
  "$SKILL_ROOT/src/shell/support/self-build.sh" \
  "$SKILL_ROOT/src/shell/support/self-rebuild.sh" \
  "$SKILL_ROOT/src/shell/support/self-status.sh" \
  "$SKILL_ROOT/src/shell/support/self-doctor.sh" \
  "$SKILL_ROOT/src/shell/test/native-runtime-gate.sh" \
  "$SKILL_ROOT/scripts/kob" \
  "$SKILL_ROOT/scripts/kano-backlog" \
  "$SKILL_ROOT/src/shell/core/kano-backlog"
do
  _name="${_f#$SKILL_ROOT/}"
  if bash -n "$_f" 2>/dev/null; then
    _ok "syntax ok: $_name"
  else
    _fail "syntax error: $_name"
  fi
done

# ---------------------------------------------------------------------------
# 3. No Python fallback
# ---------------------------------------------------------------------------
echo ""
echo "--- Python-free checks ---"

if grep -Eq "PYTHON_BIN|kano_backlog_cli\\.cli:main|KANO_BACKLOG_ALLOW_PYTHON_FALLBACK" \
     "$SKILL_ROOT/src/shell/core/kano-backlog" 2>/dev/null; then
  _fail "launcher contains Python fallback code"
else
  _ok "launcher has no Python fallback"
fi

if [[ -e "$SKILL_ROOT/pyproject.toml" ]]; then
  _fail "pyproject.toml found (should not exist)"
else
  _ok "no pyproject.toml"
fi

_py_files="$(
  find "$SKILL_ROOT" -type f \( -name '*.py' -o -name '*.pyi' \) \
    ! -path "$SKILL_ROOT/src/cpp/out/*" \
    ! -path "$SKILL_ROOT/src/shell/release/post_release_verify.py" \
    ! -path "$SKILL_ROOT/_ws/*" \
    ! -path "$SKILL_ROOT/.git/*" \
    ! -path "$SKILL_ROOT/.kano/*" \
    ! -path "$SKILL_ROOT/.pixi/*" \
    ! -path "$SKILL_ROOT/node_modules/*" \
    2>/dev/null || true
)"
if [[ -n "$_py_files" ]]; then
  _fail "Python source or typing stub files remain outside the bounded release-only verifier:"
  printf '%s\n' "$_py_files" | while read -r _l; do printf '       %s\n' "$_l"; done
else
  _ok "no Python source or typing stub files remain outside the bounded release-only verifier"
fi

# ---------------------------------------------------------------------------
# 4. Wrong-product launcher check
# ---------------------------------------------------------------------------
echo ""
echo "--- Wrong-product launcher check ---"

for _launcher in kog kog.bat kano-git kano-git.bat; do
  if [[ -e "$SKILL_ROOT/scripts/$_launcher" ]]; then
    _fail "wrong-product launcher present: scripts/$_launcher (belongs to kano-git-master-skill)"
  else
    _ok "not present: scripts/$_launcher"
  fi
done

# ---------------------------------------------------------------------------
# 5. Default build config check
# ---------------------------------------------------------------------------
echo ""
echo "--- Build config defaults ---"

if grep -q 'MODE="\${1:-release}"' "$SKILL_ROOT/src/shell/support/self-build.sh" 2>/dev/null; then
  _ok "self-build.sh default: release"
else
  _fail "self-build.sh default is NOT release"
fi

if grep -q 'MODE="\${1:-release}"' "$SKILL_ROOT/src/shell/support/self-rebuild.sh" 2>/dev/null; then
  _ok "self-rebuild.sh default: release"
else
  _fail "self-rebuild.sh default is NOT release"
fi

# ---------------------------------------------------------------------------
# 6. Native binary check
# ---------------------------------------------------------------------------
echo ""
echo "--- Native binary ---"

_exe_suffix=""
case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*) _exe_suffix=".exe" ;;
esac

_active_bin=""
case "$(uname -s 2>/dev/null || printf 'unknown')" in
  MINGW*|MSYS*|CYGWIN*)
    _search_paths=(
      "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/release/kano-backlog.exe"
      "$SKILL_ROOT/src/cpp/out/bin/windows-ninja-msvc/debug/kano-backlog.exe"
      "$SKILL_ROOT/src/cpp/out/bin/windows-msbuild/release/kano-backlog.exe"
      "$SKILL_ROOT/src/cpp/out/bin/windows-msbuild/debug/kano-backlog.exe"
    )
    ;;
  Linux)
    _search_paths=(
      "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-clang/release/kano-backlog"
      "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-clang/debug/kano-backlog"
      "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/release/kano-backlog"
      "$SKILL_ROOT/src/cpp/out/bin/linux-ninja-gcc/debug/kano-backlog"
    )
    ;;
  Darwin)
    case "$(uname -m 2>/dev/null || printf 'unknown')" in
      arm64|aarch64)
        _search_paths=(
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang-arm64/release/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang-arm64/debug/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/release/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/debug/kano-backlog"
        )
        ;;
      *)
        _search_paths=(
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang-x64/release/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang-x64/debug/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/release/kano-backlog"
          "$SKILL_ROOT/src/cpp/out/bin/macos-ninja-clang/debug/kano-backlog"
        )
        ;;
    esac
    ;;
  *)
    _search_paths=()
    ;;
esac

for _c in "${_search_paths[@]+"${_search_paths[@]}"}"; do
  if [[ -f "$_c" ]]; then
    _active_bin="$_c"
    break
  fi
done

if [[ -z "$_active_bin" && -f "$SKILL_ROOT/src/cpp/out/bin/kano-backlog$_exe_suffix" ]]; then
  _active_bin="$SKILL_ROOT/src/cpp/out/bin/kano-backlog$_exe_suffix"
fi

if [[ -n "$_active_bin" ]]; then
  _ok "native binary found: ${_active_bin#$SKILL_ROOT/}"

  # Version match
  _expected="$(tr -d '\r\n' < "$SKILL_ROOT/VERSION")"
  _ver_out="$("$_active_bin" --version 2>&1 || true)"
  _detected="$(printf '%s\n' "$_ver_out" | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1 || true)"
  if [[ "$_detected" == "$_expected" ]]; then
    _ok "binary version matches VERSION ($_expected)"
  else
    _fail "binary version '$_detected' does not match VERSION '$_expected'"
  fi

  # kob --version
  if "$SKILL_ROOT/scripts/kob" --version >/dev/null 2>&1; then
    _ok "kob --version works"
  else
    _fail "kob --version failed"
  fi

  # kob doctor
  if "$SKILL_ROOT/scripts/kob" doctor >/dev/null 2>&1; then
    _ok "kob doctor works"
  else
    _fail "kob doctor failed"
  fi
else
  _warn "native binary not found; run: bash scripts/kob self build"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "==========================================="
printf 'Results: %d passed, %d failed, %d warnings\n' "$_pass" "$_fail" "$_warn"

if (( _fail > 0 )); then
  exit 1
fi
