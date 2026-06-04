#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/kob-common.sh"

usage() {
  kob_usage_block "create-workitem.sh --type <task|bug|feature|user-story|epic> --title <title> [extra kob item create args]"
}

if [[ $# -eq 0 ]]; then
  usage >&2
  exit 1
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

kob_exec item create "$@"
