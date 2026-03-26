#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

source "$SCRIPT_DIR/../common/windows_preset_build.sh"

kb_run_windows_preset "windows-ninja-clang" "windows-ninja-clang-debug" "x64"
