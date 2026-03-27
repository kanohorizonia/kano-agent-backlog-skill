#!/usr/bin/env bash
# =============================================================================
# prerequisite_macos.sh — macOS C++ build prerequisites
# =============================================================================
# Checks: Xcode/Clang, Ninja, Git.
# Delegates to kano-cpp-expert-skill if KOG_EXPERT_SKILL_ROOT is set.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/macos/prerequisite_macos.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/macos/prerequisite_macos.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/macos/prerequisite_macos.sh"
else
  for cmd in cmake ninja git clang++; do
    if ! command -v "$cmd" &>/dev/null; then
      echo "prerequisite_macos.sh: required command '$cmd' not found" >&2
      exit 1
    fi
  done
fi
