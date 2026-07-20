#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CASE_ROOT="$ROOT_DIR/.kano/tmp/kob-self-build-resolution-$$"

cleanup() {
  rm -rf "$CASE_ROOT"
}
trap cleanup EXIT

case "$(uname -m 2>/dev/null || printf 'unknown')" in
  arm64|aarch64) preset="macos-ninja-clang-arm64" ;;
  *) preset="macos-ninja-clang-x64" ;;
esac

mkdir -p \
  "$CASE_ROOT/src/shell/core" \
  "$CASE_ROOT/src/cpp/out/bin/$preset/release" \
  "$CASE_ROOT/src/cpp/out/bin/$preset/debug"
cp "$ROOT_DIR/src/shell/core/kano-backlog" "$CASE_ROOT/src/shell/core/kano-backlog"
cp "$ROOT_DIR/VERSION" "$CASE_ROOT/VERSION"

cat > "$CASE_ROOT/src/cpp/out/bin/$preset/release/kano-backlog" <<EOF
#!/usr/bin/env bash
printf 'release %s\n' '$(tr -d '\r\n' < "$ROOT_DIR/VERSION")'
EOF
cat > "$CASE_ROOT/src/cpp/out/bin/$preset/debug/kano-backlog" <<EOF
#!/usr/bin/env bash
printf 'debug %s\n' '$(tr -d '\r\n' < "$ROOT_DIR/VERSION")'
EOF
chmod +x \
  "$CASE_ROOT/src/cpp/out/bin/$preset/release/kano-backlog" \
  "$CASE_ROOT/src/cpp/out/bin/$preset/debug/kano-backlog"

output="$(cd "$CASE_ROOT" && bash src/shell/core/kano-backlog --version)"
[[ "$output" == release* ]]

echo "PASS: kob resolves host-architecture Release binary before Debug"
