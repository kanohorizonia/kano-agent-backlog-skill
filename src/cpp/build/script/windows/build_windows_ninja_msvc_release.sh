#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

source "$SCRIPT_DIR/../common/windows_preset_build.sh"

kabsd_run_windows_build "build/windows-ninja-msvc-release" "Release" "Ninja" "x64"

echo
echo "Primary local command surface: ./kob"
echo "Native compatibility binary: .agents/skills/kano/kano-agent-backlog-skill/src/cpp/build/windows-ninja-msvc-release/kano-backlog.exe"
