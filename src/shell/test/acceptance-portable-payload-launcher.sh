#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CASE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kob-portable-payload-launcher.XXXXXX")"
trap 'rm -rf "$CASE_ROOT"' EXIT

PAYLOAD_ROOT="$CASE_ROOT/KanoHorizonia.KanoBacklog"
mkdir -p "$PAYLOAD_ROOT/scripts" "$PAYLOAD_ROOT/out/bin" "$CASE_ROOT/bin"
cp "$ROOT_DIR/scripts/kob" "$PAYLOAD_ROOT/scripts/kob"
cp "$ROOT_DIR/scripts/kano-backlog" "$PAYLOAD_ROOT/scripts/kano-backlog"

cat > "$PAYLOAD_ROOT/out/bin/kano-backlog" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'portable-native:%s\n' "$*"
EOF
chmod +x \
  "$PAYLOAD_ROOT/scripts/kob" \
  "$PAYLOAD_ROOT/scripts/kano-backlog" \
  "$PAYLOAD_ROOT/out/bin/kano-backlog"

direct_kob_output="$("$PAYLOAD_ROOT/scripts/kob" version)"
[[ "$direct_kob_output" == "portable-native:version" ]]

direct_cli_output="$("$PAYLOAD_ROOT/scripts/kano-backlog" doctor)"
[[ "$direct_cli_output" == "portable-native:doctor" ]]

ln -s "$PAYLOAD_ROOT/scripts/kob" "$CASE_ROOT/bin/kob"
ln -s "$PAYLOAD_ROOT/scripts/kano-backlog" "$CASE_ROOT/bin/kano-backlog"

linked_kob_output="$("$CASE_ROOT/bin/kob" --version)"
[[ "$linked_kob_output" == "portable-native:--version" ]]

linked_cli_output="$("$CASE_ROOT/bin/kano-backlog" version)"
[[ "$linked_cli_output" == "portable-native:version" ]]

rm "$PAYLOAD_ROOT/out/bin/kano-backlog"
if "$CASE_ROOT/bin/kob" version >"$CASE_ROOT/missing.stdout" 2>"$CASE_ROOT/missing.stderr"; then
  echo "portable launcher unexpectedly succeeded without a source launcher or packaged binary" >&2
  exit 1
fi
grep -F "neither the source launcher nor a packaged kano-backlog binary was found" \
  "$CASE_ROOT/missing.stderr" >/dev/null

echo "acceptance-portable-payload-launcher: PASS"
