#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

source "$SCRIPT_DIR/../common/docker_linux_build.sh"

kob_run_linux_preset_via_docker "linux-ninja-clang" "linux-ninja-clang-release"
