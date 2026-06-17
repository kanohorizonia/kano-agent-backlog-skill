#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/lib/build-common.sh"

kano_wix_prepare_context 1
kano_wix_print_context
echo "Payload staged only; skipping wix build."
