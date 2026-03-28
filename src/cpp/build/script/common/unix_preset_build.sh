#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source local build_metadata.sh (which sources infra's build_metadata.sh)
source "$SCRIPT_DIR/build_metadata.sh"

if [[ -z "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  echo "KOB_CPP_ROOT is not set." >&2
  exit 1
fi

# =============================================================================
# kob_run_unix_build — backlog-skill wrapper around kano_cpp_run_unix_preset
# =============================================================================
kob_run_unix_build() {
  local configure_preset="$1"
  local build_preset="$2"

  kano_cpp_run_unix_preset "$configure_preset" "$build_preset" KOB
}
