#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}" ]]; then
  KOB_SHARED_BUILD_METADATA_SH="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}/shared/infra/scripts/common/build_metadata.sh"
else
  KOB_SHARED_BUILD_METADATA_SH="$SCRIPT_DIR/../../../shared/infra/scripts/common/build_metadata.sh"
fi

if [[ ! -f "$KOB_SHARED_BUILD_METADATA_SH" ]]; then
  echo "shared infra build metadata script not found: $KOB_SHARED_BUILD_METADATA_SH" >&2
  exit 1
fi

source "$KOB_SHARED_BUILD_METADATA_SH"

kob_cpp_root() {
  kano_cpp_root
}

kob_workspace_root() {
  kano_cpp_workspace_root
}

kob_resolve_self_config_value() {
  kano_cpp_resolve_self_config_value "$1"
}

kob_apply_self_build_config() {
  kano_cpp_apply_self_build_config KOB
}

kob_collect_build_metadata() {
  kano_cpp_collect_build_metadata KOB
}
