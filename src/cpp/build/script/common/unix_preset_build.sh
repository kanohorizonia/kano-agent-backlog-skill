#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source local build_metadata.sh (which sources infra's build_metadata.sh)
source "$SCRIPT_DIR/build_metadata.sh"

if [[ -z "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  echo "KOB_CPP_ROOT is not set." >&2
  exit 1
fi

KOB_CPP_ROOT_EFFECTIVE="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}"
KOB_INFRA_UNIX_PRESET_BUILD_SH=""
for candidate in \
  "$KOB_CPP_ROOT_EFFECTIVE/shared/infra/scripts/lib/unix_preset_build.sh" \
  "$KOB_CPP_ROOT_EFFECTIVE/shared/infra/scripts/common/unix_preset_build.sh" \
  "$SCRIPT_DIR/../../../shared/infra/scripts/lib/unix_preset_build.sh" \
  "$SCRIPT_DIR/../../../shared/infra/scripts/common/unix_preset_build.sh"
do
  if [[ -f "$candidate" ]]; then
    KOB_INFRA_UNIX_PRESET_BUILD_SH="$candidate"
    break
  fi
done

if [[ -z "$KOB_INFRA_UNIX_PRESET_BUILD_SH" ]]; then
  echo "shared infra Unix preset build script not found under: $KOB_CPP_ROOT_EFFECTIVE/shared/infra/scripts/{lib,common}" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$KOB_INFRA_UNIX_PRESET_BUILD_SH"

# =============================================================================
# kob_run_unix_build — backlog-skill wrapper around kano_cpp_run_unix_preset
# =============================================================================
kob_run_unix_build() {
  local configure_preset="$1"
  local build_preset="$2"

  kano_cpp_run_unix_preset "$configure_preset" "$build_preset" KOB
}
