#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_metadata.sh"

if [[ -z "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  echo "KOB_CPP_ROOT is not set." >&2
  exit 1
fi

kob_run_unix_build() {
  local configure_preset="$1"
  local build_preset="$2"
  local cpp_root

  cpp_root="$(kob_cpp_root)"
  kob_apply_self_build_config
  kob_collect_build_metadata

  (
    cd "$cpp_root"
    cmake --preset "$configure_preset"
    cmake --build --preset "$build_preset"
  )
}
