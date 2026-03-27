#!/usr/bin/env bash
# =============================================================================
# bootstrap-common.sh — Kano Agent Backlog C++ build bootstrap
# =============================================================================
# Delegates to kano-cpp-expert-skill if KOG_EXPERT_SKILL_ROOT is set.
# Otherwise uses bundled stub that checks prerequisites only.
# =============================================================================
set -euo pipefail

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/bootstrap-common.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/common/bootstrap-common.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/bootstrap-common.sh"
else
  # Stub: check cmake and ninja are available
  for cmd in cmake ninja git; do
    if ! command -v "$cmd" &>/dev/null; then
      echo "bootstrap-common.sh: required command '$cmd' not found" >&2
      exit 1
    fi
  done
fi
