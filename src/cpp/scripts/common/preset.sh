#!/usr/bin/env bash
# =============================================================================
# preset.sh — Kano Agent Backlog C++ preset resolution
# =============================================================================
# Resolves CMake preset file path from KOG_PRESET_NAME.
# Delegates to kano-cpp-expert-skill if available.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKLOG_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

resolve_preset_file() {
  local preset_name="${1:-}"
  local preset_file="${BACKLOG_ROOT}/src/cpp/CMakePresets.json"
  if [[ -f "$preset_file" ]]; then
    echo "$preset_file"
  fi
}

if [[ -n "${KOG_EXPERT_SKILL_ROOT:-}" ]] && [[ -f "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/preset.sh" ]]; then
  # shellcheck source=../../../../../.agents/skills/kano/kano-cpp-expert-skill/src/shell/build/common/preset.sh
  source "$KOG_EXPERT_SKILL_ROOT/src/shell/build/common/preset.sh"
fi
