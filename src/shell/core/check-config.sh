#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/kob-common.sh"

if [[ "${1:-}" == "--show" ]]; then
  shift
  kob_exec config show "$@"
fi

kob_exec config validate "$@"
