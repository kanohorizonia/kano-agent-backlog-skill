#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${KOG_CPP_ROOT:-}" ]]; then
  echo "KOG_CPP_ROOT is not set." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source infra's generic unix preset runner (provides kano_cpp_run_unix_preset)
# shellcheck disable=SC1091
source "$SCRIPT_DIR/../../shared/infra/scripts/common/unix_preset_build.sh"

# Source local build_metadata.sh (already sources infra via shared/infra)
if [[ -f "$SCRIPT_DIR/build_metadata.sh" ]]; then
  source "$SCRIPT_DIR/build_metadata.sh"
fi

# Wrap kano_cpp_run_unix_preset for agent-backlog (no special LLVM handling needed)
kabld_run_unix_preset() {
  local in_configure_preset="${1:-}"
  local in_build_preset="${2:-}"
  (
    cd "$KOG_CPP_ROOT"
    kog_apply_self_build_config
    kog_collect_build_metadata
    cmake --preset "$in_configure_preset"
    cmake --build --preset "$in_build_preset"
  )
}
