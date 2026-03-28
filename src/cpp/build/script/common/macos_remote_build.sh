#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

KOB_REMOTE_HOST_RESOLVER_SH="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-$SCRIPT_DIR/../../..}}/shared/infra/scripts/common/remote_host_resolver.sh"
if [[ ! -f "$KOB_REMOTE_HOST_RESOLVER_SH" ]]; then
  KOB_REMOTE_HOST_RESOLVER_SH="$SCRIPT_DIR/../../../shared/infra/scripts/common/remote_host_resolver.sh"
fi
if [[ -f "$KOB_REMOTE_HOST_RESOLVER_SH" ]]; then
  # shellcheck disable=SC1091
  source "$KOB_REMOTE_HOST_RESOLVER_SH"
fi

KOB_MACBUILDER_HOST="${KOB_MACBUILDER_HOST:-dorgon.chang@macbuilder.cobia-tailor.ts.net}"
KOB_REMOTE_BUILD_DIR="${KOB_REMOTE_BUILD_DIR:-/tmp/kano-backlog-build}"
KOB_SSH_OPTS="${KOB_SSH_OPTS:--o StrictHostKeyChecking=no -o ConnectTimeout=10}"
KOB_SSH_OPTS_RSYNC="${KOB_SSH_OPTS_RSYNC:-o StrictHostKeyChecking=no -o ConnectTimeout=10}"

KOB_CMAKE_SEARCH_PATHS='$HOME/bin/cmake/CMake.app/Contents/bin/cmake /usr/local/bin/cmake /usr/bin/cmake /opt/homebrew/bin/cmake'
KOB_NINJA_SEARCH_PATHS='$HOME/bin/ninja /usr/local/bin/ninja /usr/bin/ninja /opt/homebrew/bin/ninja'

kob_remote_build_macos() {
  local in_configure_preset="${1:-}"
  local in_build_preset="${2:-}"

  local source_repo="${KOB_CPP_ROOT:-${KABSD_CPP_ROOT:-}}"
  if [[ -z "$source_repo" ]]; then
    echo "[ERROR] KOB_CPP_ROOT is not set" >&2
    return 1
  fi

  local host_with_user=""
  local host_addr=""
  if declare -F kano_cpp_pick_remote_host >/dev/null 2>&1; then
    host_with_user="$(kano_cpp_pick_remote_host "${KANO_REMOTE_HOST_GROUP:-mac-local}" "${KANO_REMOTE_HOST_ROUTE:-auto}" "$KOB_MACBUILDER_HOST" || true)"
  fi
  if [[ -z "$host_with_user" ]]; then
    host_with_user="$KOB_MACBUILDER_HOST"
  fi
  host_addr="${host_with_user#*@}"
  echo "[INFO] Using macOS builder: $host_with_user"

  if ! ssh -o BatchMode=yes -q ${host_with_user:+${host_with_user}} "echo 'SSH OK'" 2>/dev/null; then
    if ! ssh ${KOB_SSH_OPTS} -q "$host_with_user" "echo 'SSH OK'" 2>/dev/null; then
      echo "[ERROR] Cannot connect to $host_with_user" >&2
      return 1
    fi
  fi

  echo "[INFO] Rsyncing source to $host_with_user:${KOB_REMOTE_BUILD_DIR}..."
  rsync -avz --delete \
    -e "ssh ${KOB_SSH_OPTS_RSYNC}" \
    --exclude 'out/' \
    --exclude 'build/' \
    --exclude '.git/' \
    --exclude 'node_modules/' \
    --exclude '__pycache__/' \
    --exclude '.kano/' \
    --exclude '.cache/' \
    "${source_repo}/" \
    "${host_with_user}:${KOB_REMOTE_BUILD_DIR}/" 2>&1 | tail -5

  local cmake_path
  cmake_path="$(ssh ${KOB_SSH_OPTS} "$host_with_user" "
    for cmake in ${KOB_CMAKE_SEARCH_PATHS}; do
      if [[ -x \"\$cmake\" ]]; then
        echo \"\$cmake\"
        exit 0
      fi
    done
    echo \"ERROR: cmake not found\" >&2
    exit 1
  ")" || true
  if [[ "$cmake_path" == ERROR:* ]]; then
    echo "[ERROR] $cmake_path" >&2
    return 1
  fi

  local ninja_path
  ninja_path="$(ssh ${KOB_SSH_OPTS} "$host_with_user" "
    for ninja in ${KOB_NINJA_SEARCH_PATHS}; do
      if [[ -x \"\$ninja\" ]]; then
        echo \"\$ninja\"
        exit 0
      fi
    done
    echo \"ERROR: ninja not found\" >&2
    exit 1
  ")" || true
  if [[ "$ninja_path" == ERROR:* ]]; then
    echo "[ERROR] $ninja_path" >&2
    return 1
  fi

  echo "[INFO] Remote tools: cmake=$cmake_path ninja=$ninja_path"
  echo "[INFO] Building configure='$in_configure_preset' build='$in_build_preset'..."

  ssh ${KOB_SSH_OPTS} "$host_with_user" "
    set -euo pipefail
    export PATH=\"$(dirname "$cmake_path"):$(dirname "$ninja_path"):\$PATH\"
    cd '${KOB_REMOTE_BUILD_DIR}'
    rm -rf out
    '$cmake_path' --preset '${in_configure_preset}'
    '$cmake_path' --build --preset '${in_build_preset}' -j\$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
  "

  echo "[INFO] Build complete on $host_addr"
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
