#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${KOG_CPP_ROOT:-}" ]]; then
  echo "KOG_CPP_ROOT is not set." >&2
  exit 1
fi

KABLD_UNIX_PRESET_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source infra's generic unix preset runner (provides kano_cpp_run_unix_preset)
# shellcheck disable=SC1091
source "$KABLD_UNIX_PRESET_SCRIPT_DIR/../../shared/infra/scripts/lib/unix_preset_build.sh"

# Source local build_metadata.sh (already sources infra via shared/infra)
if [[ -f "$KABLD_UNIX_PRESET_SCRIPT_DIR/build_metadata.sh" ]]; then
  source "$KABLD_UNIX_PRESET_SCRIPT_DIR/build_metadata.sh"
fi

# Wrap kano_cpp_run_unix_preset for agent-backlog (no special LLVM handling needed)
kabld_run_unix_preset() {
  local in_configure_preset="${1:-}"
  local in_build_preset="${2:-}"
  local -a cache_override_args=()

  if [[ -n "${INF_CMAKE_CACHE_ARGS_JSON:-}" ]]; then
    # shellcheck disable=SC2207
    cache_override_args+=( $(kano_cpp_infra_tool cache-args-to-cmake "$INF_CMAKE_CACHE_ARGS_JSON") )
  fi

  (
    cd "$KOG_CPP_ROOT"
    kog_apply_self_build_config
    kog_collect_build_metadata
    cmake --preset "$in_configure_preset" \
      "-DKB_PRESET_NAME=$in_configure_preset" \
      "${cache_override_args[@]}"
    if [[ "${#cache_override_args[@]}" -gt 0 ]]; then
      # CMake can discard non-toolchain cache overrides when a compiler change
      # triggers its automatic cache reset. Reassert caller-owned overrides on
      # the now-stable toolchain configuration before building.
      cmake --preset "$in_configure_preset" \
        "-DKB_PRESET_NAME=$in_configure_preset" \
        "${cache_override_args[@]}"
    fi
    cmake --build --preset "$in_build_preset"
  )
}
