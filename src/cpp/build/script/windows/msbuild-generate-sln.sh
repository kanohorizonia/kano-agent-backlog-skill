#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

source "$SCRIPT_DIR/../common/windows_preset_build.sh"

echo "Configuring Visual Studio solution with automatic vcvarsall bootstrap..."
kabsd_configure_windows_preset "windows-msbuild" "x64"
echo "Generated solution under: $KOB_CPP_ROOT/out/obj/windows-msbuild"
