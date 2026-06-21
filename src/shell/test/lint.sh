#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

show_help() {
  cat <<'EOF'
Usage: lint.sh [--help]

Runs lightweight native-contract lint checks for executable migration gates.
Python ruff/black/isort/mypy are no longer part of the supported runtime gate.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  show_help
  exit 0
fi

failed=0

check_absent() {
  local pattern="$1"
  local path="$2"
  local label="$3"
  if grep -RIn -- "$pattern" "$path" >/dev/null 2>&1; then
    echo "[FAIL] $label" >&2
    grep -RIn -- "$pattern" "$path" >&2 || true
    failed=1
  else
    echo "[PASS] $label"
  fi
}

check_present() {
  local pattern="$1"
  local path="$2"
  local label="$3"
  if grep -RIn -- "$pattern" "$path" >/dev/null 2>&1; then
    echo "[PASS] $label"
  else
    echo "[FAIL] $label" >&2
    failed=1
  fi
}

if [[ -f "$SKILL_ROOT/pyproject.toml" ]]; then
  check_absent "\\[build-system\\]\\|\\[project\\]\\|kano_backlog_cli.cli:main" "$SKILL_ROOT/pyproject.toml" "pyproject is not a Python package contract"
else
  echo "[PASS] pyproject Python package contract is absent"
fi
check_absent "PYTHON_BIN\\|kano_backlog_cli\\.cli:main\\|KANO_BACKLOG_ALLOW_PYTHON_FALLBACK" "$SKILL_ROOT/src/shell/core/kano-backlog" "launcher has no Python fallback"
if [[ -e "$SKILL_ROOT/src/python" ]]; then
  echo "[FAIL] removed Python runtime source directory is absent" >&2
  failed=1
else
  echo "[PASS] removed Python runtime source directory is absent"
fi
if [[ -e "$SKILL_ROOT/tests" || -e "$SKILL_ROOT/test_vcs.py" ]]; then
  echo "[FAIL] removed pytest oracle files are absent" >&2
  failed=1
else
  echo "[PASS] removed pytest oracle files are absent"
fi
remaining_py="$(
  find "$SKILL_ROOT" -type f \( -name '*.py' -o -name '*.pyi' \) \
    ! -path "$SKILL_ROOT/src/cpp/out/*" \
    ! -path "$SKILL_ROOT/_ws/*" \
    ! -path "$SKILL_ROOT/.git/*" \
    ! -path "$SKILL_ROOT/.kano/*" \
    ! -path "$SKILL_ROOT/.pixi/*" \
    ! -path "$SKILL_ROOT/node_modules/*" \
    2>/dev/null || true
)"
if [[ -n "$remaining_py" ]]; then
  echo "[FAIL] no Python source or typing stub files remain in the repo" >&2
  printf '%s\n' "$remaining_py" >&2
  failed=1
else
  echo "[PASS] no Python source or typing stub files remain in the repo"
fi
check_absent "^[[:space:]]*\\(python\\|pip\\)[[:space:]]*=" "$SKILL_ROOT/pixi.toml" "pixi default env has no Python runtime dependency"
check_absent "^\\[pypi-dependencies\\]\\|kano-agent-backlog-skill[[:space:]]*=[[:space:]]*{[[:space:]]*path[[:space:]]*=" "$SKILL_ROOT/pixi.toml" "pixi default env has no editable Python package"
check_absent "^[[:space:]]+- pypi:\\|conda: .*/\\(python\\|pip\\|setuptools\\|wheel\\)-" "$SKILL_ROOT/pixi.lock" "pixi lock has no Python runtime/package records"
check_present "native-runtime-gate" "$SKILL_ROOT/pixi.toml" "pixi exposes native runtime gate"

windows_error_refs="$(
  grep -RInE "Set(ErrorMode|ThreadErrorMode)|_CrtSetReportMode|_set_abort_behavior|_set_invalid_parameter_handler" \
    "$SKILL_ROOT/src/cpp/code" "$SKILL_ROOT/src/cpp/tests" 2>/dev/null |
    grep -v "noninteractive_errors.hpp" || true
)"
if [[ -n "$windows_error_refs" ]]; then
  echo "[FAIL] Windows assert/error-dialog suppression is centralized" >&2
  printf '%s\n' "$windows_error_refs" >&2
  failed=1
else
  echo "[PASS] Windows assert/error-dialog suppression is centralized"
fi

exit "$failed"
