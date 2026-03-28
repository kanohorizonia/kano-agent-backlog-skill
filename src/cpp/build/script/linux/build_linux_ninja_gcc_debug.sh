#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

exec bash "$SCRIPT_DIR/../../../shared/infra/scripts/linux/native-build.sh" "linux-ninja-gcc" "linux-ninja-gcc-debug" "KOB"
