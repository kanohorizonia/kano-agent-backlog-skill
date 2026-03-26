#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
export CMAKE_OSX_ARCHITECTURES="x86_64"

source "$SCRIPT_DIR/../common/unix_preset_build.sh"

kob_run_unix_build "macos-ninja-clang" "macos-ninja-clang-debug"
