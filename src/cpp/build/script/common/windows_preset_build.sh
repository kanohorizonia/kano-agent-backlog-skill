#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_metadata.sh"

if [[ -z "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  echo "KOB_CPP_ROOT is not set." >&2
  exit 1
fi

KOB_INFRA_WINDOWS_PRESET_IMPL="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}/shared/infra/scripts/windows/windows_preset_build.sh"
if [[ ! -f "$KOB_INFRA_WINDOWS_PRESET_IMPL" ]]; then
  echo "infra windows preset build script not found: $KOB_INFRA_WINDOWS_PRESET_IMPL" >&2
  exit 1
fi

source "$KOB_INFRA_WINDOWS_PRESET_IMPL"
