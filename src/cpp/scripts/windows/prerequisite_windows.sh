#!/usr/bin/env bash
# =============================================================================
# prerequisite_windows.sh — Windows C++ build prerequisites
# =============================================================================
# Checks: Visual Studio / MSVC toolchain, Ninja, Git.
# Delegates to kano-cpp-expert-skill if KOG_EXPERT_SKILL_ROOT is set.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/windows/prerequisite_windows.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/windows/prerequisite_windows.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/windows/prerequisite_windows.sh"
else
  # Stub: verify cmake and ninja are on PATH
  for cmd in cmake ninja git; do
    if ! command -v "$cmd" &>/dev/null; then
      echo "prerequisite_windows.sh: required command '$cmd' not found" >&2
      exit 1
    fi
  done
  if ! command -v cl &>/dev/null && ! command -v clang-cl &>/dev/null; then
    echo "prerequisite_windows.sh: C++ compiler (cl or clang-cl) not found" >&2
    exit 1
  fi
fi
