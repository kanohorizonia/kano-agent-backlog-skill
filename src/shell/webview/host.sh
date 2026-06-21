#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

port="${KANO_WEBVIEW_HOST_PORT:-8799}"
products_root="$SKILL_ROOT/../_kano/backlog/products"
if command -v cygpath >/dev/null 2>&1; then
  products_root="$(cygpath -w "$products_root")"
fi

if [[ "${KANO_WEBVIEW_OPEN:-1}" != "0" ]]; then
  (
    sleep "${KANO_WEBVIEW_OPEN_DELAY_SECONDS:-2}"
    bash "$SCRIPT_DIR/open.sh" "http://127.0.0.1:$port/"
  ) >/dev/null 2>&1 &
fi

exec bash "$SKILL_ROOT/scripts/kob" webview serve --backlog-root "$products_root" --port "$port"
