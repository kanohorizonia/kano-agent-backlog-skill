#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

source "$SCRIPT_DIR/../common/windows_preset_build.sh"

kabsd_run_windows_preset "windows-ninja-msvc" "windows-ninja-msvc-debug" "x64"

echo
echo "Primary local command surface: ./kob"
echo "Preset build dir: src/cpp/out/obj/windows-ninja-msvc"
