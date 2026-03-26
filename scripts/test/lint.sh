#!/usr/bin/env bash
# =============================================================================
# Lint & Type Check - Code quality checks
# =============================================================================
# Runs ruff, black, isort, and mypy on the skill source code.
#
# Usage:
#   bash scripts/test/lint.sh [--fix]
#   bash scripts/test/lint.sh --help
#
# Options:
#   --fix   Apply automatic fixes (ruff, black, isort). mypy does not have --fix.
#   --help  Show this help
#
# Requirements:
#   - ruff, black, isort, mypy installed (via pip install -e ".[dev]")
# =============================================================================

set -euo pipefail

# Detect script directory
if [[ -n "${BASH_SOURCE[0]:-}" ]]; then
    SCRIPT_SOURCE="${BASH_SOURCE[0]}"
else
    SCRIPT_SOURCE="$0"
fi

SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SOURCE")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Add skill's src/python/ to PYTHONPATH so tools can import packages
export PYTHONPATH="$SKILL_ROOT/src/python:${PYTHONPATH:-}"

# Colors
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    DIM='\033[2m'
    RESET='\033[0m'
else
    RED='' GREEN='' YELLOW='' BLUE='' DIM='' RESET=''
fi

log_info()  { printf "${BLUE}[INFO]${RESET}  %s\n" "$*"; }
log_pass()  { printf "${GREEN}[PASS]${RESET}  %s\n" "$*"; }
log_fail()  { printf "${RED}[FAIL]${RESET}  %s\n" "$*"; }
log_warn()  { printf "${YELLOW}[WARN]${RESET}  %s\n" "$*"; }
log_section() { printf "\n${DIM}=== %s ===${RESET}\n" "$*"; }

APPLY_FIXES=false
if [[ "${1:-}" == "--fix" ]]; then
    APPLY_FIXES=true
fi

show_help() {
    cat <<'EOF'
Usage: lint.sh [options]

Options:
  --fix   Apply automatic fixes (ruff, black, isort)
  --help  Show this help

Tools checked:
  ruff     - Linting and some auto-fixes
  black    - Code formatting
  isort    - Import sorting
  mypy     - Type checking

Examples:
  bash scripts/test/lint.sh           # Check only
  bash scripts/test/lint.sh --fix     # Check + auto-fix
EOF
}

if [[ "${1:-}" == "--help" ]]; then
    show_help
    exit 0
fi

main() {
    log_section "kano-agent-backlog-skill: Lint & Type Check"
    log_info "Skill root: $SKILL_ROOT"
    log_info "Apply fixes: $APPLY_FIXES"
    echo

    local src_dir="$SKILL_ROOT/src"
    local failed=0

    # ruff: linting
    log_section "1. ruff (linting)"
    if command -v ruff >/dev/null 2>&1; then
        local ruff_args=(check "$src_dir")
        if [[ "$APPLY_FIXES" == "true" ]]; then
            ruff_args+=( --fix)
        fi
        if ruff "${ruff_args[@]}"; then
            log_pass "ruff: OK"
        else
            log_fail "ruff: issues found"
            failed=1
        fi
    else
        log_warn "ruff not installed (pip install ruff)"
    fi

    # black: formatting
    log_section "2. black (formatting)"
    if command -v black >/dev/null 2>&1; then
        local black_args=( --check "$src_dir")
        if [[ "$APPLY_FIXES" == "true" ]]; then
            black_args=( "$src_dir")
        fi
        if black "${black_args[@]}"; then
            log_pass "black: OK"
        else
            log_fail "black: formatting issues"
            failed=1
        fi
    else
        log_warn "black not installed (pip install black)"
    fi

    # isort: import sorting
    log_section "3. isort (import sorting)"
    if command -v isort >/dev/null 2>&1; then
        local isort_args=( --check-only "$src_dir")
        if [[ "$APPLY_FIXES" == "true" ]]; then
            isort_args=( "$src_dir")
        fi
        if isort "${isort_args[@]}"; then
            log_pass "isort: OK"
        else
            log_fail "isort: import issues"
            failed=1
        fi
    else
        log_warn "isort not installed (pip install isort)"
    fi

    # mypy: type checking
    log_section "4. mypy (type checking)"
    if command -v mypy >/dev/null 2>&1; then
        if mypy "$src_dir" --no-error-summary 2>&1; then
            log_pass "mypy: OK"
        else
            log_fail "mypy: type issues found"
            failed=1
        fi
    else
        log_warn "mypy not installed (pip install mypy)"
    fi

    echo
    if [[ $failed -eq 0 ]]; then
        log_pass "Lint & type check: ALL PASSED"
    else
        log_fail "Lint & type check: SOME FAILED"
        log_info "Tip: Run with --fix to auto-fix ruff/black/isort issues"
    fi

    return $failed
}

main "$@"
