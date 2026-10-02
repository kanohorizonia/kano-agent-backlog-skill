#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"

export KANO_CPP_INFRA_CPP_ROOT="${KANO_CPP_INFRA_CPP_ROOT:-$REPO_ROOT/src/cpp}"
source "$REPO_ROOT/src/cpp/shared/infra/scripts/lib/native_tool.sh"
kano_cpp_infra_watchdog_enter "$0" "$@"
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

BACKEND_ROOT="$SMOKE_ROOT/backend-routing"
mkdir -p "$BACKEND_ROOT/pass-input" "$BACKEND_ROOT/fail-input"
cp \
  "$REPORT_ROOT/install-windows-x64/install-verification-windows-x64.json" \
  "$BACKEND_ROOT/pass-input/install-verification-windows-x64.json"
python3 - "$BACKEND_ROOT/fail-input/install-verification-linux-x64.json" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
path.write_text(
    json.dumps(
        {
            "schemaVersion": 1,
            "kind": "kano-post-release-install-verification",
            "platform": "linux-x64",
            "status": "fail",
            "cloudBackend": "github-actions",
            "selectedChannel": "",
            "artifactUrl": "",
            "cliSmokeResult": {"status": "not-run"},
        },
        indent=2,
        sort_keys=True,
    )
    + "\n",
    encoding="utf-8",
)
PY

python3 "$REPO_ROOT/src/shell/release/post_release_verify.py" aggregate-install-backends \
  --input-root "$BACKEND_ROOT/pass-input" \
  --output-dir "$BACKEND_ROOT/pass-summary" \
  --fail-if-no-pass
grep -F '"status": "pass"' "$BACKEND_ROOT/pass-summary/install-verification-backend-summary.json" >/dev/null

if python3 "$REPO_ROOT/src/shell/release/post_release_verify.py" aggregate-install-backends \
  --input-root "$BACKEND_ROOT/fail-input" \
  --output-dir "$BACKEND_ROOT/fail-summary" \
  --fail-if-no-pass; then
  echo "expected backend aggregation without PASS evidence to fail" >&2
  exit 1
fi
grep -F '"status": "fail"' "$BACKEND_ROOT/fail-summary/install-verification-backend-summary.json" >/dev/null

mkdir -p "$BACKEND_ROOT/malformed/cloud" "$BACKEND_ROOT/malformed/local"
printf '[]\n' >"$BACKEND_ROOT/malformed/cloud/install-verification-linux-x64.json"
cp \
  "$REPORT_ROOT/install-windows-x64/install-verification-windows-x64.json" \
  "$BACKEND_ROOT/malformed/local/install-verification-windows-x64.json"
python3 "$REPO_ROOT/src/shell/release/post_release_verify.py" aggregate-install-backends \
  --input-root "$BACKEND_ROOT/malformed/cloud" \
  --input-root "$BACKEND_ROOT/malformed/local" \
  --output-dir "$BACKEND_ROOT/malformed/summary" \
  --fail-if-no-pass
grep -F '"status": "invalid"' "$BACKEND_ROOT/malformed/summary/install-verification-backend-summary.json" >/dev/null
grep -F '"status": "pass"' "$BACKEND_ROOT/malformed/summary/install-verification-backend-summary.json" >/dev/null

FAKE_BIN="$BACKEND_ROOT/fake-bin"
mkdir -p "$FAKE_BIN"
cat >"$FAKE_BIN/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

case "${1:-}:${2:-}" in
  workflow:run)
    exit 0
    ;;
  run:list)
    printf '4242\tInstallVerify_linux-x64_v0.0.4_%s\n' "${KANO_FAKE_GH_CORRELATION:?}"
    ;;
  run:view)
    jq_mode="false"
    for argument in "$@"; do
      [[ "$argument" == "--jq" ]] && jq_mode="true"
    done
    run_status="${KANO_FAKE_GH_RUN_STATUS:-completed}"
    conclusion="${KANO_FAKE_GH_CONCLUSION:-success}"
    if [[ "$jq_mode" == "true" ]]; then
      printf '%s|%s|https://example.invalid/run/4242\n' "$run_status" "$conclusion"
    else
      printf '{"databaseId":4242,"status":"%s","conclusion":"%s","url":"https://example.invalid/run/4242"}\n' \
        "$run_status" "$conclusion"
    fi
    ;;
  run:download)
    output_dir=""
    while [[ $# -gt 0 ]]; do
      if [[ "$1" == "--dir" ]]; then
        output_dir="${2:-}"
        shift 2
      else
        shift
      fi
    done
    mkdir -p "$output_dir"
    python3 - "$output_dir/install-verification-linux-x64.json" "${KANO_FAKE_GH_STATUS:?}" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
status = sys.argv[2]
path.write_text(
    json.dumps(
        {
            "schemaVersion": 1,
            "kind": "kano-post-release-install-verification",
            "platform": "linux-x64",
            "status": status,
            "cloudBackend": "github-actions",
            "selectedChannel": "portable-tar" if status == "pass" else "",
            "artifactUrl": "https://example.invalid/asset.tar.gz",
            "cliSmokeResult": {"status": status},
        },
        indent=2,
        sort_keys=True,
    )
    + "\n",
    encoding="utf-8",
)
PY
    ;;
  *)
    echo "unexpected fake gh invocation: $*" >&2
    exit 2
    ;;
esac
EOF
chmod +x "$FAKE_BIN/gh"

PATH="$FAKE_BIN:$PATH" \
  KANO_FAKE_GH_CORRELATION="fixture-cloud-pass" \
  KANO_FAKE_GH_STATUS="pass" \
  KANO_FAKE_GH_RUN_STATUS="completed" \
  KANO_FAKE_GH_CONCLUSION="success" \
  KANO_INSTALL_VERIFY_RESOLVE_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_RESOLVE_DELAY_SECONDS="0" \
  KANO_INSTALL_VERIFY_COMPLETION_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_COMPLETION_DELAY_SECONDS="0" \
  bash "$REPO_ROOT/src/shell/release/09-dispatch-post-release-install-cloud.sh" \
    --repo kanohorizonia/kano-agent-backlog-skill \
    --tag v0.0.4 \
    --version 0.0.4 \
    --platform linux-x64 \
    --output-dir "$BACKEND_ROOT/cloud-pass" \
    --correlation fixture-cloud-pass
test -f "$BACKEND_ROOT/cloud-pass/cloud/backend-pass.txt"
test ! -f "$BACKEND_ROOT/cloud-pass/cloud/fallback-required.txt"

PATH="$FAKE_BIN:$PATH" \
  bash "$REPO_ROOT/src/shell/release/09-dispatch-post-release-install-cloud.sh" \
    --repo kanohorizonia/kano-agent-backlog-skill \
    --tag v0.0.4 \
    --version 0.0.4 \
    --platform linux-x64 \
    --output-dir "$BACKEND_ROOT/cloud-pass" \
    --use-cloud false
test -f "$BACKEND_ROOT/cloud-pass/cloud/backend-info.txt"
test ! -f "$BACKEND_ROOT/cloud-pass/cloud/backend-warning.txt"
test ! -e "$BACKEND_ROOT/cloud-pass/cloud/artifacts/install-verification-linux-x64.json"
if python3 "$REPO_ROOT/src/shell/release/post_release_verify.py" aggregate-install-backends \
  --input-root "$BACKEND_ROOT/cloud-pass/cloud" \
  --output-dir "$BACKEND_ROOT/cloud-pass/reuse-summary" \
  --fail-if-no-pass; then
  echo "expected helper output-root reuse to remove stale cloud PASS evidence" >&2
  exit 1
fi

PATH="$FAKE_BIN:$PATH" \
  KANO_FAKE_GH_CORRELATION="fixture-cloud-fail" \
  KANO_FAKE_GH_STATUS="fail" \
  KANO_FAKE_GH_RUN_STATUS="completed" \
  KANO_FAKE_GH_CONCLUSION="failure" \
  KANO_INSTALL_VERIFY_RESOLVE_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_RESOLVE_DELAY_SECONDS="0" \
  KANO_INSTALL_VERIFY_COMPLETION_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_COMPLETION_DELAY_SECONDS="0" \
  bash "$REPO_ROOT/src/shell/release/09-dispatch-post-release-install-cloud.sh" \
    --repo kanohorizonia/kano-agent-backlog-skill \
    --tag v0.0.4 \
    --version 0.0.4 \
    --platform linux-x64 \
    --output-dir "$BACKEND_ROOT/cloud-fail" \
    --correlation fixture-cloud-fail
test -f "$BACKEND_ROOT/cloud-fail/cloud/fallback-required.txt"

PATH="$FAKE_BIN:$PATH" \
  KANO_FAKE_GH_CORRELATION="fixture-cloud-timeout" \
  KANO_FAKE_GH_STATUS="pass" \
  KANO_FAKE_GH_RUN_STATUS="in_progress" \
  KANO_FAKE_GH_CONCLUSION="" \
  KANO_INSTALL_VERIFY_RESOLVE_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_RESOLVE_DELAY_SECONDS="0" \
  KANO_INSTALL_VERIFY_COMPLETION_ATTEMPTS="1" \
  KANO_INSTALL_VERIFY_COMPLETION_DELAY_SECONDS="0" \
  bash "$REPO_ROOT/src/shell/release/09-dispatch-post-release-install-cloud.sh" \
    --repo kanohorizonia/kano-agent-backlog-skill \
    --tag v0.0.4 \
    --version 0.0.4 \
    --platform linux-x64 \
    --output-dir "$BACKEND_ROOT/cloud-timeout" \
    --correlation fixture-cloud-timeout
grep -F 'bounded polling window' "$BACKEND_ROOT/cloud-timeout/cloud/fallback-required.txt" >/dev/null

grep -F 'fileExists("${env.VERIFY_ROOT}/cloud/fallback-required.txt")' \
  "$REPO_ROOT/.jenkins/PostRelease_Install_Verify.Jenkinsfile" >/dev/null
grep -F 'stage('\''Require passing backend evidence'\'')' \
  "$REPO_ROOT/.jenkins/PostRelease_Install_Verify.Jenkinsfile" >/dev/null
grep -F 'deleteDir()' \
  "$REPO_ROOT/.jenkins/PostRelease_Install_Verify.Jenkinsfile" >/dev/null
grep -F 'inputs.correlation_id' \
  "$REPO_ROOT/.github/workflows/post-release-install-verify.yml" >/dev/null

echo "release_install_verification_smoke: PASS"
echo "report: $REPORT_ROOT"
