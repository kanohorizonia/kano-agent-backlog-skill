#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
SMOKE_ROOT="${KANO_RELEASE_INSTALL_VERIFY_SMOKE_ROOT:-$REPO_ROOT/src/cpp/.kano/tmp/release-install-verification-smoke}"
PAYLOAD_ROOT="$SMOKE_ROOT/payload/kano-agent-backlog-skill"
ASSET_ROOT="$SMOKE_ROOT/assets"
REPORT_ROOT="$SMOKE_ROOT/report"
RELEASE_JSON="$SMOKE_ROOT/release.json"

rm -rf -- "$SMOKE_ROOT"
mkdir -p "$PAYLOAD_ROOT/scripts" "$ASSET_ROOT" "$REPORT_ROOT"

cat > "$PAYLOAD_ROOT/scripts/kob" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "version" || "${1:-}" == "--version" ]]; then
  echo "kano-backlog 0.0.4-smoke"
  exit 0
fi
echo "kob smoke"
EOF
chmod +x "$PAYLOAD_ROOT/scripts/kob"
cp "$PAYLOAD_ROOT/scripts/kob" "$PAYLOAD_ROOT/scripts/kano-backlog"
cat > "$PAYLOAD_ROOT/scripts/kob.bat" <<'EOF'
@echo off
if "%1"=="version" (
  echo kano-backlog 0.0.4-smoke
  exit /b 0
)
if "%1"=="--version" (
  echo kano-backlog 0.0.4-smoke
  exit /b 0
)
echo kob smoke
EOF
cp "$PAYLOAD_ROOT/scripts/kob.bat" "$PAYLOAD_ROOT/scripts/kano-backlog.bat"

tar -C "$SMOKE_ROOT/payload" -czf "$ASSET_ROOT/kano-backlog-linux-x64-main-0.0.4.999-Release-cli.tar.gz" kano-agent-backlog-skill
tar -C "$SMOKE_ROOT/payload" -czf "$ASSET_ROOT/kano-backlog-windows-x64-main-0.0.4.999-Release-cli.tar.gz" kano-agent-backlog-skill
touch "$ASSET_ROOT/kano-backlog-windows-x64-main-0.0.4.999-Release-cli.msi"
touch "$ASSET_ROOT/kano-backlog-macos-arm64-main-0.0.4.999-Release-cli.tar.gz"
touch "$ASSET_ROOT/kano-backlog-macos-x64-main-0.0.4.999-Release-cli.tar.gz"

asset_root_posix="$(cd -- "$ASSET_ROOT" && pwd -P)"
cat > "$RELEASE_JSON" <<EOF
{
  "tag_name": "v0.0.4",
  "draft": false,
  "prerelease": false,
  "html_url": "https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/tag/v0.0.4",
  "assets": [
    {
      "name": "kano-backlog-linux-x64-main-0.0.4.999-Release-cli.tar.gz",
      "browser_download_url": "file://${asset_root_posix}/kano-backlog-linux-x64-main-0.0.4.999-Release-cli.tar.gz",
      "local_path": "${asset_root_posix}/kano-backlog-linux-x64-main-0.0.4.999-Release-cli.tar.gz",
      "digest": "sha256:fixture-linux"
    },
    {
      "name": "kano-backlog-windows-x64-main-0.0.4.999-Release-cli.msi",
      "browser_download_url": "https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/download/v0.0.4/kano-backlog-windows-x64-main-0.0.4.999-Release-cli.msi",
      "digest": "sha256:fixture-msi"
    },
    {
      "name": "kano-backlog-windows-x64-main-0.0.4.999-Release-cli.tar.gz",
      "browser_download_url": "file://${asset_root_posix}/kano-backlog-windows-x64-main-0.0.4.999-Release-cli.tar.gz",
      "local_path": "${asset_root_posix}/kano-backlog-windows-x64-main-0.0.4.999-Release-cli.tar.gz",
      "digest": "sha256:fixture-windows-tar"
    },
    {
      "name": "kano-backlog-macos-arm64-main-0.0.4.999-Release-cli.tar.gz",
      "browser_download_url": "https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/download/v0.0.4/kano-backlog-macos-arm64-main-0.0.4.999-Release-cli.tar.gz",
      "digest": "sha256:fixture-macos-arm64"
    },
    {
      "name": "kano-backlog-macos-x64-main-0.0.4.999-Release-cli.tar.gz",
      "browser_download_url": "https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/download/v0.0.4/kano-backlog-macos-x64-main-0.0.4.999-Release-cli.tar.gz",
      "digest": "sha256:fixture-macos-x64"
    }
  ]
}
EOF

bash "$REPO_ROOT/src/shell/release/07-recheck-release-assets-msi.sh" \
  --release-json "$RELEASE_JSON" \
  --output-dir "$REPORT_ROOT/release-assets"

bash "$REPO_ROOT/src/shell/release/04-validate-homebrew-owned-tap.sh" \
  --release-json "$RELEASE_JSON" \
  --output-dir "$REPORT_ROOT/homebrew"

bash "$REPO_ROOT/src/shell/release/08-post-release-install-verify.sh" \
  --release-json "$RELEASE_JSON" \
  --platform windows-x64 \
  --output-dir "$REPORT_ROOT/install-windows-x64" \
  --no-enable-winget \
  --no-enable-msi \
  --execute-install \
  --fail-if-no-pass

grep -F '"status": "pass"' "$REPORT_ROOT/release-assets/release-asset-msi-recheck.json" >/dev/null
grep -F '"status": "validated-without-install"' "$REPORT_ROOT/homebrew/homebrew-owned-tap-validation.json" >/dev/null
grep -F '"selectedChannel": "portable-tar"' "$REPORT_ROOT/install-windows-x64/install-verification-windows-x64.json" >/dev/null
grep -F '"status": "pass"' "$REPORT_ROOT/install-windows-x64/install-verification-windows-x64.json" >/dev/null
grep -F 'def caveats' "$REPORT_ROOT/homebrew/kano-backlog.rb" >/dev/null

echo "release_install_verification_smoke: PASS"
echo "report: $REPORT_ROOT"
