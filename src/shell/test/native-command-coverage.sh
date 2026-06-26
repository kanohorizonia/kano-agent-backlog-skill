#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
STRICT=0
COVERAGE_TIMEOUT_SECONDS="${KANO_BACKLOG_COMMAND_COVERAGE_TIMEOUT_SECONDS:-10}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --strict)
      STRICT=1
      shift
      ;;
    --help|-h)
      cat <<'EOF'
Usage: native-command-coverage.sh [--strict]

Reports native CLI command coverage and verifies native command surfaces only.
EOF
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

find_native_bin() {
  local exe_suffix=""
  local -a presets=(
    windows-ninja-msvc
    windows-ninja-clang
    windows-msbuild
    linux-ninja-clang
    linux-ninja-gcc
    macos-ninja-clang
    macos-ninja-clang-x64
    macos-ninja-clang-arm64
  )
  local -a configs=(release relwithdebinfo minsizerel debug)
  local preset
  local config
  local candidate

  case "$(uname -s 2>/dev/null || printf 'unknown')" in
    MINGW*|MSYS*|CYGWIN*) exe_suffix=".exe" ;;
  esac

  for preset in "${presets[@]}"; do
    for config in "${configs[@]}"; do
      candidate="$SKILL_ROOT/src/cpp/out/bin/$preset/$config/kano-backlog$exe_suffix"
      if [[ -f "$candidate" ]]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  done

  return 1
}

NATIVE_BIN="$(find_native_bin)"

run_native_cli() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "${COVERAGE_TIMEOUT_SECONDS}s" "$NATIVE_BIN" "$@"
  else
    "$NATIVE_BIN" "$@"
  fi
}

has_native_command() {
  local command_name="$1"
  local output
  output="$(run_native_cli "$command_name" --help 2>&1 || true)"
  if [[ "$command_name" == "item" ]] && printf '%s\n' "$output" | grep -Eq "kano-backlog[[:space:]]+workitem([[:space:]]|$)"; then
    return 0
  fi
  printf '%s\n' "$output" | grep -Eq "kano-backlog[[:space:]]+${command_name}([[:space:]]|$)"
}

native_admin_help_works() {
  local command_name="$1"
  run_native_cli admin "$command_name" --help >/dev/null 2>&1
}

native_config_help_works() {
  local command_name="$1"
  run_native_cli config "$command_name" --help >/dev/null 2>&1
}

native_workitem_help_works() {
  local command_name="$1"
  run_native_cli workitem "$command_name" --help >/dev/null 2>&1
}

native_tokenizer_help_works() {
  local command_name="$1"
  run_native_cli tokenizer "$command_name" --help >/dev/null 2>&1
}

native_snapshot_help_works() {
  local command_name="$1"
  run_native_cli snapshot "$command_name" --help >/dev/null 2>&1
}

native_links_help_works() {
  local command_name="$1"
  run_native_cli links "$command_name" --help >/dev/null 2>&1
}

native_topic_help_works() {
  local command_name="$1"
  run_native_cli topic "$command_name" --help >/dev/null 2>&1
}

native_topic_template_help_works() {
  local command_name="$1"
  run_native_cli topic template "$command_name" --help >/dev/null 2>&1
}

native_topic_snapshot_help_works() {
  local command_name="$1"
  run_native_cli topic snapshot "$command_name" --help >/dev/null 2>&1
}

commands=(
  admin
  workitem
  item
  state
  worklog
  view
  doctor
  snapshot
  workset
  evidence
  assumptions
  inspect
  topic
  config
  changelog
  benchmark
  embedding
  chunks
  search
  tokenizer
  gui
  webview
  orphan
  links
  repo-hygiene
  export
)

declare -A status=(
  [admin]="native covered"
  [workitem]="native covered"
  [item]="native covered"
  [state]="native covered"
  [worklog]="native covered"
  [view]="native covered"
  [doctor]="native covered"
  [snapshot]="native covered"
  [workset]="native covered"
  [evidence]="native covered"
  [assumptions]="native covered"
  [inspect]="native covered"
  [topic]="native covered"
  [config]="native covered"
  [changelog]="native covered"
  [benchmark]="native covered"
  [embedding]="native covered"
  [chunks]="native covered"
  [search]="native covered"
  [tokenizer]="native covered"
  [gui]="native covered"
  [webview]="native covered"
  [orphan]="native covered"
  [links]="native covered"
  [repo-hygiene]="native covered"
  [export]="native covered"
)

admin_commands=(
  index
  demo
  validate
  links
  items
  adr
  schema
  meta
  sandbox
  persona
  release
)

config_commands=(
  profiles
  pipeline
  show
  export
  validate
  init
  migrate-json
)

workitem_commands=(
  create
  check-ready
  add-decision
  trash
  set-ready
  update-state
  attach-artifact
)

tokenizer_commands=(
  config
  validate
  test
  create-example
  migrate
  diagnose
  health
  cache-stats
  accuracy
  cache-clear
  env-vars
  dependencies
  install-guide
  adapter-status
  status
  benchmark
  compare
  install
  recommend
  list-models
  telemetry
  telemetry-export
  telemetry-clear
  monitor
  health-check
  alerts
)

snapshot_commands=(
  create
  report
)

links_commands=(
  fix
  restore-from-vcs
  remap-id
  remap-ref
  normalize-ids
  replace-id
  replace-target
)

topic_commands=(
  create
  add
  template
  snapshot
  pin
  add-snippet
  distill
  audit
  decision-audit
  close
  cleanup
  switch
  export-context
  list
  add-reference
  remove-reference
  list-active
  show-state
  sync-opencode-plan
  resolve-opencode-plan
  split
  merge
  migrate
  cleanup-legacy
  migrate-filenames
)

topic_template_commands=(
  list
  show
  validate
)

topic_snapshot_commands=(
  create
  list
  restore
  cleanup
)

nested_command_specs=(
  "evidence add|native covered"
  "evidence list|native covered"
  "evidence get|native covered"
  "evidence delete|native covered"
  "evidence summary|native covered"
  "assumptions list|native covered"
  "assumptions generate|native covered"
  "workset init|native covered"
  "workset refresh|native covered"
  "workset next|native covered"
  "workset promote|native covered"
  "workset cleanup|native covered"
  "workset list|native covered"
  "workset detect-adr|native covered"
  "state transition|native covered"
  "worklog append|native covered"
  "view refresh|native covered"
  "orphan check|native covered"
  "orphan suggest|native covered"
  "benchmark run|native covered"
  "chunks build|native covered"
  "chunks query|native covered"
  "chunks build-repo|native covered"
  "chunks query-repo|native covered"
  "chunks build-repo-vectors|native covered"
  "chunks build-status|native covered"
  "embedding build|native covered"
  "embedding query|native covered"
  "embedding status|native covered"
  "search query|native covered"
  "search hybrid|native covered"
  "admin index build|native routed"
  "admin index refresh|native routed"
  "admin index status|native routed"
  "admin schema check|native routed"
  "admin schema fix|native routed"
  "admin validate uids|native routed"
  "admin validate repo-layout|native routed"
  "admin validate links|native routed"
  "admin adr create|native routed"
  "admin adr fix-uids|native routed"
  "admin meta add-ticketing-guidance|native routed"
  "admin demo seed|native routed"
  "admin persona summary|native covered"
  "admin persona report|native covered"
  "admin sandbox init|native covered"
  "admin release check|native covered"
  "admin items trash|native covered"
  "admin items set-parent|native covered"
)

declare -A admin_status=(
  [index]="native routed"
  [demo]="native routed"
  [validate]="native routed"
  [links]="native routed"
  [items]="native covered"
  [adr]="native routed"
  [schema]="native routed"
  [meta]="native routed"
  [sandbox]="native covered"
  [persona]="native covered"
  [release]="native covered"
)

declare -A config_status=(
  [profiles]="native covered"
  [pipeline]="native covered"
  [show]="native complete"
  [export]="native covered"
  [validate]="native covered"
  [init]="native covered"
  [migrate-json]="native covered"
)

declare -A workitem_status=(
  [create]="native covered"
  [check-ready]="native covered"
  [add-decision]="native covered"
  [trash]="native covered"
  [set-ready]="native covered"
  [update-state]="native covered"
  [attach-artifact]="native covered"
)

declare -A tokenizer_status=(
  [config]="native covered"
  [validate]="native covered"
  [test]="native covered"
  [create-example]="native covered"
  [migrate]="native covered"
  [diagnose]="native covered"
  [health]="native covered"
  [cache-stats]="native covered"
  [accuracy]="native covered"
  [cache-clear]="native covered"
  [env-vars]="native covered"
  [dependencies]="native covered"
  [install-guide]="native covered"
  [adapter-status]="native covered"
  [status]="native covered"
  [benchmark]="native covered"
  [compare]="native covered"
  [install]="native covered"
  [recommend]="native covered"
  [list-models]="native covered"
  [telemetry]="native covered"
  [telemetry-export]="native covered"
  [telemetry-clear]="native covered"
  [monitor]="native covered"
  [health-check]="native covered"
  [alerts]="native covered"
)

declare -A snapshot_status=(
  [create]="native covered"
  [report]="native covered"
)

declare -A links_status=(
  [fix]="native covered"
  [restore-from-vcs]="native covered"
  [remap-id]="native covered"
  [remap-ref]="native covered"
  [normalize-ids]="native covered"
  [replace-id]="native complete"
  [replace-target]="native complete"
)

declare -A topic_status=(
  [create]="native covered"
  [add]="native covered"
  [template]="native covered"
  [snapshot]="native covered"
  [pin]="native covered"
  [add-snippet]="native covered"
  [distill]="native covered"
  [audit]="native covered"
  [decision-audit]="native covered"
  [close]="native covered"
  [cleanup]="native covered"
  [switch]="native covered"
  [export-context]="native covered"
  [list]="native covered"
  [add-reference]="native covered"
  [remove-reference]="native covered"
  [list-active]="native covered"
  [show-state]="native covered"
  [sync-opencode-plan]="native covered"
  [resolve-opencode-plan]="native covered"
  [split]="native covered"
  [merge]="native covered"
  [migrate]="native covered"
  [cleanup-legacy]="native covered"
  [migrate-filenames]="native covered"
)

declare -A topic_template_status=(
  [list]="native covered"
  [show]="native covered"
  [validate]="native covered"
)

declare -A topic_snapshot_status=(
  [create]="native covered"
  [list]="native covered"
  [restore]="native covered"
  [cleanup]="native covered"
)

failures=0

printf '# Native CLI Coverage Report\n\n'
printf 'Native: `%s`\n\n' "$NATIVE_BIN"
printf '| Command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${commands[@]}"; do
  native_seen="no"
  if has_native_command "$command_name"; then
    native_seen="yes"
  fi
  printf '| `%s` | %s | %s |\n' "$command_name" "$native_seen" "${status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Snapshot Commands\n\n'
printf '| Snapshot command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${snapshot_commands[@]}"; do
  native_seen="no"
  if native_snapshot_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `snapshot %s` | %s | %s |\n' "$command_name" "$native_seen" "${snapshot_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Tokenizer Commands\n\n'
printf '| Tokenizer command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${tokenizer_commands[@]}"; do
  native_seen="no"
  if native_tokenizer_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `tokenizer %s` | %s | %s |\n' "$command_name" "$native_seen" "${tokenizer_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Workitem Commands\n\n'
printf '| Workitem command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${workitem_commands[@]}"; do
  native_seen="no"
  if native_workitem_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `workitem %s` | %s | %s |\n' "$command_name" "$native_seen" "${workitem_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Topic Commands\n\n'
printf '| Topic command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${topic_commands[@]}"; do
  native_seen="no"
  if native_topic_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `topic %s` | %s | %s |\n' "$command_name" "$native_seen" "${topic_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Topic Template Commands\n\n'
printf '| Topic template command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${topic_template_commands[@]}"; do
  native_seen="no"
  if native_topic_template_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `topic template %s` | %s | %s |\n' "$command_name" "$native_seen" "${topic_template_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Topic Snapshot Commands\n\n'
printf '| Topic snapshot command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${topic_snapshot_commands[@]}"; do
  native_seen="no"
  if native_topic_snapshot_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `topic snapshot %s` | %s | %s |\n' "$command_name" "$native_seen" "${topic_snapshot_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Additional Nested Commands\n\n'
printf '| Command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for spec in "${nested_command_specs[@]}"; do
  command_path="${spec%%|*}"
  command_status="${spec#*|}"
  native_seen="no"
  read -r -a command_words <<< "$command_path"
  if run_native_cli "${command_words[@]}" --help >/dev/null 2>&1; then
    native_seen="yes"
  fi
  printf '| `%s` | %s | %s |\n' "$command_path" "$native_seen" "$command_status"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Links Commands\n\n'
printf '| Links command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${links_commands[@]}"; do
  native_seen="no"
  if native_links_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `links %s` | %s | %s |\n' "$command_name" "$native_seen" "${links_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Config Commands\n\n'
printf '| Config command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${config_commands[@]}"; do
  native_seen="no"
  if native_config_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `config %s` | %s | %s |\n' "$command_name" "$native_seen" "${config_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'
printf '## Admin Commands\n\n'
printf '| Admin command | Native help | Status |\n'
printf '| --- | --- | --- |\n'

for command_name in "${admin_commands[@]}"; do
  native_seen="no"
  if native_admin_help_works "$command_name"; then
    native_seen="yes"
  fi
  printf '| `admin %s` | %s | %s |\n' "$command_name" "$native_seen" "${admin_status[$command_name]}"

  if [[ "$STRICT" -eq 1 && "$native_seen" != "yes" ]]; then
    failures=$((failures + 1))
  fi
done

printf '\n'

if ! run_native_cli repo-hygiene check --archive-safe >/dev/null; then
  echo 'Native repo-hygiene compatibility check failed.'
  failures=$((failures + 1))
fi

EXPORT_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/kano-backlog-export.XXXXXX")"
if ! run_native_cli export --single --no-validate-release-archive --output "$EXPORT_TMP_DIR" >/dev/null; then
  echo 'Native export compatibility check failed.'
  failures=$((failures + 1))
fi
rm -rf "$EXPORT_TMP_DIR"

if [[ "$STRICT" -eq 1 && "$failures" -gt 0 ]]; then
  echo "Native command coverage strict mode failed with $failures issue(s)." >&2
  exit 1
fi

echo "Native command coverage report completed with $failures strict issue(s)."
