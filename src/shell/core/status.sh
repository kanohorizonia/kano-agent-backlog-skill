#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib/kob-common.sh"

echo "[kob] doctor"
kob_run doctor
echo
echo "[kob] topic list"
kob_run topic list
echo
echo "[kob] workset list"
kob_run workset list
