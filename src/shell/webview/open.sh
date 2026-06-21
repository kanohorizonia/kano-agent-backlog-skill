#!/usr/bin/env bash
set -euo pipefail

url="${1:-http://127.0.0.1:${KANO_WEBVIEW_HOST_PORT:-8799}/}"

if command -v powershell.exe >/dev/null 2>&1; then
  KANO_WEBVIEW_OPEN_URL="$url" powershell.exe -NoProfile -Command 'Start-Process $env:KANO_WEBVIEW_OPEN_URL'
elif command -v powershell >/dev/null 2>&1; then
  KANO_WEBVIEW_OPEN_URL="$url" powershell -NoProfile -Command 'Start-Process $env:KANO_WEBVIEW_OPEN_URL'
elif command -v open >/dev/null 2>&1; then
  open "$url"
elif command -v xdg-open >/dev/null 2>&1; then
  xdg-open "$url"
else
  printf 'Open %s\n' "$url"
fi
