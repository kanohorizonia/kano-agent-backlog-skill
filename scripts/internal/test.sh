#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

ctest --test-dir "$SKILL_ROOT/src/cpp/out/obj/windows-ninja-msvc" --output-on-failure "$@"
