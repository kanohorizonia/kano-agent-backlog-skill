#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/kob-common.sh"

usage() {
  kob_usage_block "init-sandbox.sh <name> --product <name> --agent <id> [extra kob sandbox init args]"
}

if [[ $# -eq 0 ]]; then
  usage >&2
  exit 1
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

kob_exec sandbox init "$@"
