#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source infra's build_metadata if available via submodule
KOG_SHARED_BUILD_METADATA_SH="${KOG_CPP_ROOT:-$SCRIPT_DIR/../../../cpp/shared/infra}/shared/infra/scripts/common/build_metadata.sh"
if [[ ! -f "$KOG_SHARED_BUILD_METADATA_SH" ]]; then
  KOG_SHARED_BUILD_METADATA_SH="$SCRIPT_DIR/../../../cpp/shared/infra/scripts/common/build_metadata.sh"
fi
if [[ -f "$KOG_SHARED_BUILD_METADATA_SH" ]]; then
  # shellcheck disable=SC1090
  source "$KOG_SHARED_BUILD_METADATA_SH"
fi

# Agent-backlog-specific helper wrappers (delegates to infra if available)
_kog_trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

kog_apply_self_build_config() {
  if declare -f kano_cpp_apply_self_build_config >/dev/null 2>&1; then
    kano_cpp_apply_self_build_config KABSD
  fi
}

kog_collect_build_metadata() {
  if declare -f kano_cpp_collect_build_metadata >/dev/null 2>&1; then
    kano_cpp_collect_build_metadata KABSD
  fi
}
