#!/usr/bin/env bash
# =============================================================================
# prerequisite_linux.sh — Linux C++ build prerequisites
# =============================================================================
# Checks: GCC/Clang, Ninja, Git.
# Delegates to kano-cpp-expert-skill if KOG_EXPERT_SKILL_ROOT is set.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/linux/prerequisite_linux.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/linux/prerequisite_linux.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/linux/prerequisite_linux.sh"
else
  for cmd in cmake ninja git g++ clang++; do
    if ! command -v "$cmd" &>/dev/null; then
      echo "prerequisite_linux.sh: required command '$cmd' not found" >&2
      exit 1
    fi
  done
fi
