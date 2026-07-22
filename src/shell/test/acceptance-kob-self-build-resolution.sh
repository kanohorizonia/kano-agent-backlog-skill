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
  "$CASE_ROOT/_ws/generated" \
  "$CASE_ROOT/node_modules/vendor" \
  "$CASE_ROOT/scripts" \
  "$CASE_ROOT/src/cpp" \
  "$CASE_ROOT/src/shell/core" \
  "$CASE_ROOT/src/shell/release" \
  "$CASE_ROOT/src/shell/support" \
  "$CASE_ROOT/src/shell/test" \
  "$CASE_ROOT/src/cpp/out/bin/$preset/release" \
  "$CASE_ROOT/src/cpp/out/bin/$preset/debug"
cp "$ROOT_DIR/src/shell/core/kano-backlog" "$CASE_ROOT/src/shell/core/kano-backlog"
cp "$ROOT_DIR/src/shell/support/self-doctor.sh" "$CASE_ROOT/src/shell/support/self-doctor.sh"
cp "$ROOT_DIR/VERSION" "$CASE_ROOT/VERSION"

cat > "$CASE_ROOT/src/shell/support/self-build.sh" <<'EOF'
#!/usr/bin/env bash
MODE="${1:-release}"
EOF
cp "$CASE_ROOT/src/shell/support/self-build.sh" "$CASE_ROOT/src/shell/support/self-rebuild.sh"
for script in \
  "$CASE_ROOT/src/shell/support/self-status.sh" \
  "$CASE_ROOT/src/shell/test/native-runtime-gate.sh" \
  "$CASE_ROOT/scripts/kob" \
  "$CASE_ROOT/scripts/kano-backlog"
do
  cat > "$script" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
done
chmod +x "$CASE_ROOT/scripts/kob" "$CASE_ROOT/scripts/kano-backlog"
printf '%s\n' '# fixture' > "$CASE_ROOT/pixi.toml"
printf '%s\n' '# fixture' > "$CASE_ROOT/src/cpp/CMakeLists.txt"
printf '%s\n' '# bounded release verifier fixture' > "$CASE_ROOT/src/shell/release/post_release_verify.py"
printf '%s\n' '# generated workspace fixture' > "$CASE_ROOT/_ws/generated/legacy.py"
printf '%s\n' '# third-party fixture' > "$CASE_ROOT/node_modules/vendor/tool.py"

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

doctor_output="$(cd "$CASE_ROOT" && bash src/shell/support/self-doctor.sh)"
grep -q 'no Python source or typing stub files remain outside the bounded release-only verifier' <<< "$doctor_output"
grep -q "native binary found: src/cpp/out/bin/$preset/release/kano-backlog" <<< "$doctor_output"

echo "PASS: kob and self doctor resolve host-architecture Release binaries and bounded source exclusions"
