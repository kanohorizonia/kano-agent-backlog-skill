#!/usr/bin/env bash
# =============================================================================
# Full Test Suite - Comprehensive tests with coverage
# =============================================================================
# Runs pytest with coverage reporting. Slower than quick-test.sh (~2-5 min).
#
# Usage:
#   bash scripts/test/run-all-tests.sh [--cleanup]
#   bash scripts/test/run-all-tests.sh --help
#
# Options:
#   --cleanup   Remove coverage artifacts after report generation
#   --verbose   Show verbose pytest output
#   --help      Show this help
#
# Requirements:
#   - Python 3.8+ with pytest, pytest-cov installed
#   - Run from skill root or any subdirectory
#
# Coverage report:
#   HTML report generated at: .coverage_html/index.html
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

# Add skill's src/python/ to PYTHONPATH so tests can import packages
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

CLEANUP=false
VERBOSE=false

show_help() {
    cat <<'EOF'
Usage: run-all-tests.sh [options]

Options:
  --cleanup   Remove coverage artifacts after report generation
  --verbose   Show verbose pytest output
  --help      Show this help

Examples:
  bash scripts/test/run-all-tests.sh
  bash scripts/test/run-all-tests.sh --cleanup
  bash scripts/test/run-all-tests.sh --verbose
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cleanup) CLEANUP=true; shift ;;
        --verbose|-v) VERBOSE=true; shift ;;
        --help|-h) show_help; exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            show_help >&2
            exit 1
            ;;
    esac
done

main() {
    log_section "kano-agent-backlog-skill: Full Test Suite"
    log_info "Skill root: $SKILL_ROOT"
    log_info "Python: $(python --version 2>&1)"
    echo

    # Check dependencies
    for tool in pytest python; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            log_fail "$tool not found"
            exit 1
        fi
    done

    if ! python -m pytest --version >/dev/null 2>&1; then
        log_fail "pytest not found. Install: pip install pytest pytest-cov"
        exit 1
    fi

    local test_dir="$SKILL_ROOT/tests"
    if [[ ! -d "$test_dir" ]]; then
        log_fail "Test directory not found: $test_dir"
        exit 1
    fi

    local failed=0
    local coverage_dir="$SKILL_ROOT/.coverage_html"

    # Run pytest with coverage
    log_section "Running pytest with coverage"

    local pytest_args=(
        --cov=kano_backlog_core
        --cov=kano_backlog_ops
        --cov=kano_backlog_cli
        --cov-report=term-missing
        "--cov-report=html:$coverage_dir"
        --tb=short
    )

    # Ignore performance utils (not a unit test)
    pytest_args+=("--ignore=$SKILL_ROOT/tests/performance_utils.py")

    if [[ "$VERBOSE" == "true" ]]; then
        pytest_args+=(-v)
    else
        pytest_args+=(-q)
    fi

    log_info "Coverage report: $coverage_dir/index.html"

    if python -m pytest "${pytest_args[@]}"; then
        log_pass "All tests passed"
    else
        failed=1
        log_fail "Tests failed"
    fi

    echo
    if [[ -d "$coverage_dir" ]]; then
        log_info "Coverage report: file://$coverage_dir/index.html"
    fi

    # Cleanup coverage artifacts if requested
    if [[ "$CLEANUP" == "true" ]]; then
        log_section "Cleaning up coverage artifacts"
        rm -rf "$SKILL_ROOT/.coverage" "$coverage_dir" "$SKILL_ROOT/htmlcov" 2>/dev/null || true
        log_info "Coverage artifacts removed"
    fi

    echo
    if [[ $failed -eq 0 ]]; then
        log_pass "Full test suite: PASSED"
    else
        log_fail "Full test suite: FAILED"
    fi

    return $failed
}

main "$@"
