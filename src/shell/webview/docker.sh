#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
COMPOSE_FILE="$SKILL_ROOT/docker-compose.webview.yml"
SERVICE_NAME="webview"
url="http://127.0.0.1:${KANO_WEBVIEW_HOST_PORT:-8799}/"

cd "$SKILL_ROOT"
docker compose -f "$COMPOSE_FILE" up -d --build "$SERVICE_NAME"

container_id="$(docker compose -f "$COMPOSE_FILE" ps -q "$SERVICE_NAME")"
if [[ -n "$container_id" ]]; then
  status=""
  for _ in $(seq 1 "${KANO_WEBVIEW_DOCKER_HEALTH_ATTEMPTS:-60}"); do
    status="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$container_id" 2>/dev/null || true)"
    case "$status" in
      healthy|running)
        break
        ;;
      unhealthy|exited|dead)
        docker compose -f "$COMPOSE_FILE" logs --tail=80 "$SERVICE_NAME" >&2 || true
        exit 1
        ;;
    esac
    sleep 1
  done
  if [[ "$status" != "healthy" && "$status" != "running" ]]; then
    docker compose -f "$COMPOSE_FILE" logs --tail=80 "$SERVICE_NAME" >&2 || true
    exit 1
  fi
fi

if [[ "${KANO_WEBVIEW_OPEN:-1}" != "0" ]]; then
  bash "$SCRIPT_DIR/open.sh" "$url"
else
  printf 'Webview available at %s\n' "$url"
fi
docker compose -f "$COMPOSE_FILE" ps "$SERVICE_NAME"
