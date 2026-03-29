#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

KOB_INFRA_MACOS_REMOTE_BUILD_SH="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-$SCRIPT_DIR/../../..}}/shared/infra/scripts/common/macos_remote_build.sh"
if [[ ! -f "$KOB_INFRA_MACOS_REMOTE_BUILD_SH" ]]; then
  KOB_INFRA_MACOS_REMOTE_BUILD_SH="$SCRIPT_DIR/../../../shared/infra/scripts/common/macos_remote_build.sh"
fi
if [[ ! -f "$KOB_INFRA_MACOS_REMOTE_BUILD_SH" ]]; then
  echo "shared infra macOS remote build script not found: $KOB_INFRA_MACOS_REMOTE_BUILD_SH" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$KOB_INFRA_MACOS_REMOTE_BUILD_SH"

KOB_MACBUILDER_HOST="${KOB_MACBUILDER_HOST:-dorgon.chang@macbuilder.cobia-tailor.ts.net}"
KOB_REMOTE_BUILD_DIR="${KOB_REMOTE_BUILD_DIR:-/tmp/kano-backlog-build}"
KOB_SSH_OPTS="${KOB_SSH_OPTS:--o StrictHostKeyChecking=no -o ConnectTimeout=10}"
KOB_SSH_OPTS_RSYNC="${KOB_SSH_OPTS_RSYNC:--o StrictHostKeyChecking=no -o ConnectTimeout=10}"

kob_remote_build_macos() {
  local in_configure_preset="${1:-}"
  local in_build_preset="${2:-}"
  local source_repo="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}"

  if [[ -z "$source_repo" ]]; then
    echo "[ERROR] KOB_CPP_ROOT is not set" >&2
    return 1
  fi

  kano_cpp_remote_build_macos \
    "$source_repo" \
    "$KOB_REMOTE_BUILD_DIR" \
    "$KOB_MACBUILDER_HOST" \
    "$in_configure_preset" \
    "$in_build_preset" \
    "$KOB_SSH_OPTS" \
    "$KOB_SSH_OPTS_RSYNC"
}

kob_detect_host_and_build_macos() {
  local in_configure_preset="$1"
  local in_build_preset="$2"
  local host_os
  host_os="$(uname -s 2>/dev/null || true)"

  case "$host_os" in
    Darwin)
      echo "[INFO] macOS host detected -> native build"
      kob_run_unix_build "$in_configure_preset" "$in_build_preset"
      ;;
    *)
      echo "[INFO] Non-macOS host detected -> remote build via macBuilder"
      kob_remote_build_macos "$in_configure_preset" "$in_build_preset"
      ;;
  esac
}
