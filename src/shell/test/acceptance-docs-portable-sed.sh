#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PREPARE_SCRIPT="$ROOT_DIR/src/shell/docs/02-prepare-content.sh"
CASE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kob-docs-portable-sed.XXXXXX")"

cleanup() {
  rm -rf "$CASE_ROOT"
}
trap cleanup EXIT

SHIM_BIN="$CASE_ROOT/bin"
SKILL_DIR="$CASE_ROOT/skill"
DEMO_DIR="$CASE_ROOT/demo"
REAL_SED="$(command -v sed)"
mkdir -p "$SHIM_BIN" "$SKILL_DIR/docs" "$DEMO_DIR"

cat > "$SHIM_BIN/sed" <<EOF
#!/usr/bin/env bash
if [[ "\${KANO_TEST_SED_FAIL:-0}" == "1" ]]; then
  echo "injected sed failure" >&2
  exit 91
fi
if [[ "\${1:-}" == "-i" || "\${1:-}" == -i* ]]; then
  echo "non-portable sed -i invocation" >&2
  exit 92
fi
exec "$REAL_SED" "\$@"
EOF
chmod +x "$SHIM_BIN/sed"

cat > "$SKILL_DIR/README.md" <<'EOF'
# Fixture README

[License](LICENSE)
[Quick start](docs/quick-start.md)
EOF
cat > "$SKILL_DIR/SKILL.md" <<'EOF'
# Fixture skill

[Agent quick start](docs/agent-quick-start.md)
EOF
printf '# Fixture home\n' > "$SKILL_DIR/docs/index.md"

FAIL_BUILD="$CASE_ROOT/build-fail"
if PATH="$SHIM_BIN:$PATH" KANO_TEST_SED_FAIL=1 \
  bash "$PREPARE_SCRIPT" "$ROOT_DIR" "$DEMO_DIR" "$SKILL_DIR" "$FAIL_BUILD" \
  > "$CASE_ROOT/failure.log" 2>&1; then
  echo "FAIL: content preparation hid an injected sed failure" >&2
  exit 1
fi

PASS_BUILD="$CASE_ROOT/build-pass"
PATH="$SHIM_BIN:$PATH" \
  bash "$PREPARE_SCRIPT" "$ROOT_DIR" "$DEMO_DIR" "$SKILL_DIR" "$PASS_BUILD" \
  > "$CASE_ROOT/success.log" 2>&1

grep -F '](https://github.com/kanohorizonia/kano-agent-backlog-skill/blob/main/LICENSE)' \
  "$PASS_BUILD/content_quartz/skill/readme.md" >/dev/null
grep -F '](../guides/quick-start.md)' \
  "$PASS_BUILD/content_quartz/skill/readme.md" >/dev/null
grep -F '](../guides/agent-quick-start.md)' \
  "$PASS_BUILD/content_quartz/skill/guide.md" >/dev/null

echo "PASS: docs content rewrites avoid sed -i and propagate sed failures"
