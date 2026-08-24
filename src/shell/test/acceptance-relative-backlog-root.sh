#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CASE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kob-relative-root.XXXXXX")"
CASE_ROOT="$(cd "$CASE_ROOT" && pwd -P)"

cleanup() {
  rm -rf "$CASE_ROOT"
}
trap cleanup EXIT

expect_output_contains() {
  local expected="$1"
  if [[ "$output" != *"$expected"* ]]; then
    printf 'FAIL: expected <%s> in launcher output:\n%s\n' "$expected" "$output" >&2
    exit 1
  fi
}

mkdir -p \
  "$CASE_ROOT/skill/src/shell/core" \
  "$CASE_ROOT/workspace/_kano/backlog/.kano"
cp "$ROOT_DIR/src/shell/core/kano-backlog" "$CASE_ROOT/skill/src/shell/core/kano-backlog"
cp "$ROOT_DIR/VERSION" "$CASE_ROOT/skill/VERSION"

fake_bin="$CASE_ROOT/fake-kano-backlog"
cat > "$fake_bin" <<EOF
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then printf 'kano-backlog %s\n' '$(tr -d '\r\n' < "$ROOT_DIR/VERSION")'; exit 0; fi
printf 'cwd=%s\n' "\$PWD"
printf 'args='; printf '<%s>' "\$@"; printf '\n'
EOF
chmod +x "$fake_bin"
export KANO_BACKLOG_BINARY="$fake_bin"
printf '[products.test]\nprefix = "TST"\nbacklog_root = "products/test"\n' > "$CASE_ROOT/workspace/_kano/backlog/.kano/backlog_config.toml"

output="$(cd "$CASE_ROOT/workspace" && bash "$CASE_ROOT/skill/src/shell/core/kano-backlog" doctor --backlog-root _kano/backlog)"
expect_output_contains "cwd=$CASE_ROOT/workspace/_kano/backlog"
expect_output_contains "<--backlog-root><$CASE_ROOT/workspace/_kano/backlog>"
if [[ "$output" == *"$CASE_ROOT/workspace/_kano/backlog/_kano/backlog"* ]]; then
  echo "FAIL: relative backlog root was resolved after launcher cwd changed" >&2
  exit 1
fi

windows_drive_root='C:\Users\example\backlog'
output="$(cd "$CASE_ROOT/workspace" && bash "$CASE_ROOT/skill/src/shell/core/kano-backlog" doctor --backlog-root "$windows_drive_root")"
expect_output_contains "<--backlog-root><$windows_drive_root>"

windows_unc_root='\\server\share\backlog'
output="$(cd "$CASE_ROOT/workspace" && bash "$CASE_ROOT/skill/src/shell/core/kano-backlog" doctor --backlog-root="$windows_unc_root")"
expect_output_contains "<--backlog-root=$windows_unc_root>"

echo "PASS: launcher resolves relative roots once and preserves native Windows absolute roots"
