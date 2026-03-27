#!/usr/bin/env bash
# =============================================================================
# output-layout.sh — Kano Agent Backlog C++ output layout helpers
# =============================================================================
# Provides KOG_BIN_ROOT, KOG_LIB_ROOT, KOG_OBJ_ROOT conventions.
# Delegates to kano-cpp-expert-skill if available.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKLOG_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

: "${KOG_BUILD_ROOT:=${BACKLOG_ROOT}/src/cpp/out}"
: "${KOG_PRESET_NAME:=unknown-preset}"

KOG_BIN_ROOT="${KOG_BUILD_ROOT}/bin/${KOG_PRESET_NAME}"
KOG_LIB_ROOT="${KOG_BUILD_ROOT}/lib/${KOG_PRESET_NAME}"
KOG_OBJ_ROOT="${KOG_BUILD_ROOT}/obj/${KOG_PRESET_NAME}"

export KOG_BIN_ROOT KOG_LIB_ROOT KOG_OBJ_ROOT

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/output-layout.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/common/output-layout.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/output-layout.sh"
fi
