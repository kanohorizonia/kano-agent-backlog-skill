#!/usr/bin/env bash
# =============================================================================
# env.sh — Kano Agent Backlog C++ build environment
# =============================================================================
# Resolves KOG_PRESET_NAME, KOG_CONFIG, KOG_BUILD_ROOT from environment.
# Delegates to kano-cpp-expert-skill if available.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKLOG_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${KOG_BUILD_ROOT:=${BACKLOG_ROOT}/src/cpp/out}"
: "${KOG_PRESET_NAME:=unknown-preset}"
: "${KOG_CONFIG:=Release}"

export KOG_BUILD_ROOT KOG_PRESET_NAME KOG_CONFIG

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/env.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/common/env.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/env.sh"
fi
