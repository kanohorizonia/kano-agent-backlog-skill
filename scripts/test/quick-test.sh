#!/usr/bin/env bash
# =============================================================================
# Quick Test - Smoke test for kano-agent-backlog-skill
# =============================================================================
# Runs pytest without coverage for fast feedback (~30 seconds).
# Use run-all-tests.sh for full test suite with coverage.
#
# Usage:
#   bash scripts/test/quick-test.sh [--verbose]
#
# Requirements:
#   - Python 3.8+ with pytest installed
#   - Run from skill root or any subdirectory
# =============================================================================

set -euo pipefail

# Detect script directory (works when sourced or executed directly)
if [[ -n "${BASH_SOURCE[0]:-}" ]]; then
    SCRIPT_SOURCE="${BASH_SOURCE[0]}"
else
    SCRIPT_SOURCE="$0"
fi

SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_SOURCE")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Add skill's src/python/ to PYTHONPATH so tests can import packages
# (skill uses src/python-layout; packages are under src/python/kano_backlog_*/)
export PYTHONPATH="$SKILL_ROOT/src/python:${PYTHONPATH:-}"

# Colors (disable if not a terminal)
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    DIM='\033[2m'
    RESET='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    DIM=''
    RESET=''
fi

log_info()  { printf "${BLUE}[INFO]${RESET}  %s\n" "$*"; }
log_pass()  { printf "${GREEN}[PASS]${RESET}  %s\n" "$*"; }
log_fail()  { printf "${RED}[FAIL]${RESET}  %s\n" "$*"; }
log_warn()  { printf "${YELLOW}[WARN]${RESET}  %s\n" "$*"; }
log_section() { printf "\n${DIM}=== %s ===${RESET}\n" "$*"; }

VERBOSE=false
if [[ "${1:-}" == "--verbose" ]] || [[ "${1:-}" == "-v" ]]; then
    VERBOSE=true
fi

main() {
    log_section "kano-agent-backlog-skill: Quick Test"
    log_info "Skill root: $SKILL_ROOT"
    log_info "Python: $(python --version 2>&1)"
    log_info "Pytest: $(python -m pytest --version 2>&1 | head -1)"
    echo

    # Check pytest is available
    if ! python -m pytest --version >/dev/null 2>&1; then
        log_fail "pytest not found. Install with: pip install pytest"
        exit 1
    fi

    # Check test directory exists
    local test_dir="$SKILL_ROOT/tests"
    if [[ ! -d "$test_dir" ]]; then
        log_fail "Test directory not found: $test_dir"
        exit 1
    fi

    local failed=0
    local test_count=0

    # Run pytest (no coverage, no cache, fast)
    log_section "Running pytest (smoke mode)"

    local pytest_args=(
        --tb=short
        -q
        "--ignore=$SKILL_ROOT/tests/performance_utils.py"
    )

    if [[ "$VERBOSE" == "true" ]]; then
        pytest_args+=(-v)
    fi

    if python -m pytest "${pytest_args[@]}"; then
        test_count=$(python -m pytest "${pytest_args[@]}" --co -q 2>/dev/null | grep -c "test_" || echo "0")
        log_pass "All quick tests passed"
    else
        failed=1
        log_fail "Quick tests failed"
    fi

    echo
    if [[ $failed -eq 0 ]]; then
        log_pass "Quick test suite: PASSED"
    else
        log_fail "Quick test suite: FAILED"
    fi

    return $failed
}

main "$@"
