#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LAUNCHER="${KOB_ROOT_TEST_LAUNCHER:-$SKILL_ROOT/src/shell/core/kano-backlog}"
CASE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kob-root-termination.XXXXXX")"
CASE_ROOT="$(cd "$CASE_ROOT" && pwd -P)"
RESULT_FILE="$CASE_ROOT/result"
resolver_pid=""

cleanup() {
  if [[ -n "$resolver_pid" ]] && kill -0 "$resolver_pid" 2>/dev/null; then
    kill "$resolver_pid" 2>/dev/null || true
    wait "$resolver_pid" 2>/dev/null || true
  fi
  # Only known fixture files and empty directories created by this test.
  rm -f "$RESULT_FILE" "$CASE_ROOT/project/.kano/backlog_config.toml" \
    "$CASE_ROOT/shared/_kano/backlog/.kano/backlog_config.toml" \
    "$CASE_ROOT/root-probe/.kano/backlog_config.toml"
  rmdir "$CASE_ROOT/project/child" "$CASE_ROOT/project/.kano" "$CASE_ROOT/project" \
    "$CASE_ROOT/shared/child" "$CASE_ROOT/shared/_kano/backlog/.kano" \
    "$CASE_ROOT/shared/_kano/backlog" "$CASE_ROOT/shared/_kano" "$CASE_ROOT/shared" \
    "$CASE_ROOT/fallback" "$CASE_ROOT/root-probe/.kano" "$CASE_ROOT/root-probe" \
    "$CASE_ROOT" 2>/dev/null || true
}
trap cleanup EXIT

resolver_source="$(awk '
  /^resolve_workspace_root\(\) \{/ { capture = 1 }
  capture { print }
  capture && /^}$/ { exit }
' "$LAUNCHER")"
[[ -n "$resolver_source" ]] || { echo "FAIL: launcher resolver was not found" >&2; exit 1; }
eval "$resolver_source"

dirname() {
  case "$parent_mode" in
    dot) printf '.\n' ;;
    unchanged) printf '%s\n' "$1" ;;
    posix-root) printf '/\n' ;;
    real) command dirname "$1" ;;
    *) echo "Unknown test parent mode" >&2; return 1 ;;
  esac
}

run_case() {
  local label="$1" parent_mode="$2" invocation="$3" expected="$4"
  (
    PWD="$invocation"
    if [[ "$parent_mode" == posix-root ]]; then
      # Map just the two filesystem probes into a fixture. This exercises an
      # apparent config at '/' without writing into the machine's actual root.
      ROOT_PROBE_PREFIX="$CASE_ROOT/root-probe"
      eval "$(sed 's|-f "$start_dir/|-f "$ROOT_PROBE_PREFIX$start_dir/|g' <<<"$resolver_source")"
    fi
    resolve_workspace_root
  ) >"$RESULT_FILE" &
  resolver_pid=$!
  for _ in {1..200}; do
    if ! kill -0 "$resolver_pid" 2>/dev/null; then
      if ! wait "$resolver_pid"; then
        resolver_pid=""
        echo "FAIL: $label resolver exited unsuccessfully" >&2
        return 1
      fi
      resolver_pid=""
      break
    fi
    sleep 0.01
  done
  if [[ -n "$resolver_pid" ]]; then
    echo "FAIL: $label resolver did not terminate within the bounded wait" >&2
    return 1
  fi
  local actual
  actual="$(cat "$RESULT_FILE")"
  if [[ "$parent_mode" == real ]]; then
    # Windows-native dirname may spell the same path as C:/ instead of /c/.
    actual="$(cd "$actual" && pwd -P)"
    expected="$(cd "$expected" && pwd -P)"
  fi
  if [[ "$actual" != "$expected" ]]; then
    printf 'FAIL: %s returned <%s>, expected <%s>\n' "$label" "$actual" "$expected" >&2
    return 1
  fi
  echo "PASS: $label"
}

mkdir -p "$CASE_ROOT/project/.kano" "$CASE_ROOT/project/child" \
  "$CASE_ROOT/shared/_kano/backlog/.kano" "$CASE_ROOT/shared/child" \
  "$CASE_ROOT/fallback" "$CASE_ROOT/root-probe/.kano"
touch "$CASE_ROOT/project/.kano/backlog_config.toml" \
  "$CASE_ROOT/shared/_kano/backlog/.kano/backlog_config.toml" \
  "$CASE_ROOT/root-probe/.kano/backlog_config.toml"

run_case 'drive-root parent dot' dot 'D:/' 'D:/'
run_case 'drive-root parent unchanged' unchanged 'D:/' 'D:/'
run_case 'project config at invocation' real "$CASE_ROOT/project" "$CASE_ROOT/project"
run_case 'project config at ancestor' real "$CASE_ROOT/project/child" "$CASE_ROOT/project"
run_case 'shared config at invocation' real "$CASE_ROOT/shared" "$CASE_ROOT/shared/_kano/backlog"
run_case 'shared config at ancestor' real "$CASE_ROOT/shared/child" "$CASE_ROOT/shared/_kano/backlog"
run_case 'invocation-directory fallback' dot "$CASE_ROOT/fallback" "$CASE_ROOT/fallback"
run_case 'POSIX root config stays excluded' posix-root '/root-probe-child' '/root-probe-child'
