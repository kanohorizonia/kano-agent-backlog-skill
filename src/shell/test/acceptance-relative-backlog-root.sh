#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CASE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kob-relative-root.XXXXXX")"

cleanup() {
  rm -rf "$CASE_ROOT"
}
trap cleanup EXIT

case "$(uname -s):$(uname -m)" in
  Darwin:arm64|Darwin:aarch64) preset="macos-ninja-clang-arm64" ;;
  Darwin:*) preset="macos-ninja-clang-x64" ;;
  Linux:*) preset="linux-ninja-gcc" ;;
  *) echo "SKIP: unsupported shell acceptance host"; exit 0 ;;
esac

mkdir -p \
  "$CASE_ROOT/skill/src/shell/core" \
  "$CASE_ROOT/skill/src/cpp/out/bin/$preset/release" \
  "$CASE_ROOT/workspace/_kano/backlog/.kano"
cp "$ROOT_DIR/src/shell/core/kano-backlog" "$CASE_ROOT/skill/src/shell/core/kano-backlog"
cp "$ROOT_DIR/VERSION" "$CASE_ROOT/skill/VERSION"

cat > "$CASE_ROOT/skill/src/cpp/out/bin/$preset/release/kano-backlog" <<EOF
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then printf 'kano-backlog %s\n' '$(tr -d '\r\n' < "$ROOT_DIR/VERSION")'; exit 0; fi
printf 'cwd=%s\n' "\$PWD"
printf 'args='; printf '<%s>' "\$@"; printf '\n'
EOF
chmod +x "$CASE_ROOT/skill/src/cpp/out/bin/$preset/release/kano-backlog"
printf '[products.test]\nprefix = "TST"\nbacklog_root = "products/test"\n' > "$CASE_ROOT/workspace/_kano/backlog/.kano/backlog_config.toml"

output="$(cd "$CASE_ROOT/workspace" && bash "$CASE_ROOT/skill/src/shell/core/kano-backlog" view refresh --backlog-root _kano/backlog --product test --agent codex)"
grep -F "cwd=$CASE_ROOT/workspace/_kano/backlog" <<<"$output" >/dev/null
grep -F "<--backlog-root><$CASE_ROOT/workspace/_kano/backlog>" <<<"$output" >/dev/null
if grep -F "$CASE_ROOT/workspace/_kano/backlog/_kano/backlog" <<<"$output" >/dev/null; then
  echo "FAIL: relative backlog root was resolved after launcher cwd changed" >&2
  exit 1
fi

echo "PASS: launcher resolves explicit relative backlog root against invocation cwd exactly once"
