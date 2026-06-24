#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

ARTIFACT_ROOT="${KANO_WEBVIEW_SMOKE_ARTIFACT_DIR:-$SKILL_ROOT/_ws/test-output/webview-smoke}"
PORT="${KANO_WEBVIEW_SMOKE_PORT:-${KANO_WEBVIEW_HOST_PORT:-8799}}"
BASE_URL="${KANO_WEBVIEW_SMOKE_BASE_URL:-${KANO_WEBVIEW_BASE_URL:-}}"
WAIT_ATTEMPTS="${KANO_WEBVIEW_SMOKE_WAIT_ATTEMPTS:-60}"
WAIT_DELAY_SECONDS="${KANO_WEBVIEW_SMOKE_WAIT_DELAY_SECONDS:-1}"
CURL_MAX_TIME_SECONDS="${KANO_WEBVIEW_SMOKE_CURL_MAX_TIME_SECONDS:-20}"

MANIFEST_PATH="$ARTIFACT_ROOT/manifest.txt"
HOST_LOG_PATH="$ARTIFACT_ROOT/host.log"

STARTED_HOST=0
HOST_PID=""
FAILURE_COUNT=0
FAILURE_SUMMARY=""

timestamp_utc() {
  date -u +"%Y-%m-%dT%H:%M:%SZ"
}

normalize_base_url() {
  local value="$1"
  value="${value%/}"
  printf '%s\n' "$value"
}

append_manifest() {
  local label="$1"
  local url="$2"
  local output_path="$3"
  local status_text="$4"
  local detail="$5"
  printf '%s | url=%s | output=%s | status=%s | detail=%s | timestamp=%s\n' \
    "$label" "$url" "$output_path" "$status_text" "$detail" "$(timestamp_utc)" >> "$MANIFEST_PATH"
}

record_failure() {
  local label="$1"
  local detail="$2"
  FAILURE_COUNT=$((FAILURE_COUNT + 1))
  FAILURE_SUMMARY+="  - ${label}: ${detail}"$'\n'
}

summarize_and_exit_if_failed() {
  if [[ "$FAILURE_COUNT" -ne 0 ]]; then
    printf 'Webview smoke artifacts failed.\nArtifacts: %s\nManifest: %s\nHost log: %s\n%s' \
      "$ARTIFACT_ROOT" "$MANIFEST_PATH" "$HOST_LOG_PATH" "$FAILURE_SUMMARY" >&2
    exit 1
  fi
}

cleanup() {
  if [[ "$STARTED_HOST" -eq 1 && -n "$HOST_PID" ]]; then
    kill "$HOST_PID" >/dev/null 2>&1 || true
    wait "$HOST_PID" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

rm -rf "$ARTIFACT_ROOT"
mkdir -p "$ARTIFACT_ROOT"

wait_for_health() {
  local url="$BASE_URL/healthz"
  local attempt=0
  local http_code="000"

  while (( attempt < WAIT_ATTEMPTS )); do
    attempt=$((attempt + 1))
    if [[ "$STARTED_HOST" -eq 1 ]] && ! kill -0 "$HOST_PID" >/dev/null 2>&1; then
      record_failure "healthz" "host process exited before readiness; see $HOST_LOG_PATH"
      append_manifest "healthz" "$url" "$ARTIFACT_ROOT/healthz.txt" "host-exited" "$HOST_LOG_PATH"
      return 1
    fi

    set +e
    http_code="$(curl --silent --show-error --output /dev/null --write-out '%{http_code}' --max-time "$CURL_MAX_TIME_SECONDS" "$url" 2>>"$HOST_LOG_PATH")"
    local curl_exit=$?
    set -e

    if [[ "$curl_exit" -eq 0 && "$http_code" =~ ^2[0-9][0-9]$ ]]; then
      return 0
    fi

    sleep "$WAIT_DELAY_SECONDS"
  done

  record_failure "healthz" "service did not become ready at $url; last_http_status=$http_code; see $HOST_LOG_PATH"
  append_manifest "healthz" "$url" "$ARTIFACT_ROOT/healthz.txt" "not-ready" "last_http_status=$http_code; host_log=$HOST_LOG_PATH"
  return 1
}

fail_if_port_is_already_serving() {
  local url="$BASE_URL/healthz"
  local output_path="$ARTIFACT_ROOT/preflight-healthz.txt"
  local stderr_path="$ARTIFACT_ROOT/preflight-healthz.stderr.txt"
  local http_code="000"

  set +e
  http_code="$(curl --silent --show-error --output "$output_path" --write-out '%{http_code}' --max-time 3 "$url" 2>"$stderr_path")"
  local curl_exit=$?
  set -e

  if [[ "$curl_exit" -eq 0 ]]; then
    local detail="existing service detected; set KANO_WEBVIEW_SMOKE_BASE_URL to reuse it or KANO_WEBVIEW_SMOKE_PORT to choose a free port"
    append_manifest "preflight" "$url" "$output_path" "http-$http_code" "$detail"
    record_failure "preflight" "$detail; base_url=$BASE_URL"
    summarize_and_exit_if_failed
  fi
}

capture_endpoint() {
  local label="$1"
  local route="$2"
  local output_name="$3"
  local url="$BASE_URL$route"
  local output_path="$ARTIFACT_ROOT/$output_name"
  local headers_path="$ARTIFACT_ROOT/${label}.headers.txt"
  local stderr_path="$ARTIFACT_ROOT/${label}.curl-stderr.txt"
  local http_code="000"

  set +e
  http_code="$(curl --silent --show-error --location --dump-header "$headers_path" --output "$output_path" --write-out '%{http_code}' --max-time "$CURL_MAX_TIME_SECONDS" "$url" 2>"$stderr_path")"
  local curl_exit=$?
  set -e

  local detail="ok"
  if [[ -s "$stderr_path" ]]; then
    detail="$(tr '\r\n' '  ' < "$stderr_path" | sed 's/[[:space:]]\+/ /g')"
  fi

  if [[ "$curl_exit" -ne 0 ]]; then
    append_manifest "$label" "$url" "$output_path" "curl-exit-$curl_exit" "$detail"
    record_failure "$label" "curl exited $curl_exit; artifact=$output_path; stderr=$stderr_path"
    return 1
  fi

  if [[ ! "$http_code" =~ ^2[0-9][0-9]$ ]]; then
    append_manifest "$label" "$url" "$output_path" "http-$http_code" "$detail"
    record_failure "$label" "http $http_code; artifact=$output_path; headers=$headers_path"
    return 1
  fi

  if [[ ! -s "$output_path" ]]; then
    append_manifest "$label" "$url" "$output_path" "http-$http_code" "empty-body"
    record_failure "$label" "empty response body; artifact=$output_path"
    return 1
  fi

  append_manifest "$label" "$url" "$output_path" "http-$http_code" "$detail"
  return 0
}

if [[ -n "$BASE_URL" ]]; then
  BASE_URL="$(normalize_base_url "$BASE_URL")"
  printf 'webview smoke artifact source: external (%s)\n' "$BASE_URL" > "$HOST_LOG_PATH"
else
  BASE_URL="http://127.0.0.1:$PORT"
  printf 'webview smoke artifact preflight: checking for an existing service on %s\n' "$BASE_URL" > "$HOST_LOG_PATH"
  fail_if_port_is_already_serving
  printf 'webview smoke artifact source: started host (%s)\n' "$BASE_URL" > "$HOST_LOG_PATH"
  printf 'starting host with KANO_WEBVIEW_OPEN=0 on port %s\n' "$PORT" >> "$HOST_LOG_PATH"
  KANO_WEBVIEW_OPEN=0 KANO_WEBVIEW_HOST_PORT="$PORT" bash "$SCRIPT_DIR/host.sh" >> "$HOST_LOG_PATH" 2>&1 &
  HOST_PID="$!"
  STARTED_HOST=1
fi

printf 'webview smoke artifacts\n' > "$MANIFEST_PATH"
printf 'artifact_root=%s\n' "$ARTIFACT_ROOT" >> "$MANIFEST_PATH"
printf 'base_url=%s\n' "$BASE_URL" >> "$MANIFEST_PATH"
printf 'host_mode=%s\n\n' "$([[ "$STARTED_HOST" -eq 1 ]] && printf 'started' || printf 'external')" >> "$MANIFEST_PATH"

if wait_for_health; then
  capture_endpoint "root" "/" "root.html" || true
  capture_endpoint "healthz" "/healthz" "healthz.txt" || true
  capture_endpoint "items-all-limit-10" "/api/items?product=all&limit=10" "items-all-limit-10.json" || true
fi

summarize_and_exit_if_failed

printf 'Webview smoke artifacts written to %s\n' "$ARTIFACT_ROOT"
printf '  manifest: %s\n' "$MANIFEST_PATH"
printf '  root: %s\n' "$ARTIFACT_ROOT/root.html"
printf '  healthz: %s\n' "$ARTIFACT_ROOT/healthz.txt"
printf '  items: %s\n' "$ARTIFACT_ROOT/items-all-limit-10.json"
