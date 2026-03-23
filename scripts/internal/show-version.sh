#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPO_ROOT="$(cd "$SKILL_ROOT/../../../.." && pwd)"

if [[ "${1:-}" == "--short" ]]; then
  tr -d '\r\n' < "$REPO_ROOT/VERSION"
  printf '\n'
  exit 0
fi

printf 'kob version: %s\n' "$(tr -d '\r\n' < "$REPO_ROOT/VERSION")"
