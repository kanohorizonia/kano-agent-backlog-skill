pipeline {
    agent { label 'lightweight' }
    options {
        timestamps()
        timeout(time: 120, unit: 'MINUTES')
        disableConcurrentBuilds()
    }
    parameters {
        string(name: 'RELEASE_TAG', defaultValue: 'v0.0.4', description: 'GitHub Release tag to verify.')
        string(name: 'RELEASE_VERSION', defaultValue: '0.0.4', description: 'Release version expected by package-manager formulas.')
        string(name: 'GITHUB_REPOSITORY', defaultValue: 'kanohorizonia/kano-agent-backlog-skill', description: 'Owner/repo containing the release assets.')
        choice(name: 'PLATFORM', choices: ['linux-x64', 'linux-arm64', 'macos-x64', 'macos-arm64', 'windows-x64', 'windows-arm64'], description: 'Platform to verify in this job run.')
        booleanParam(name: 'USE_GITHUB_ACTIONS_CLOUD', defaultValue: true, description: 'Prefer the GitHub Actions clean-runner backend.')
        booleanParam(name: 'ALLOW_LOCAL_BACKEND', defaultValue: true, description: 'Run local Jenkins verification if cloud dispatch is unavailable.')
        booleanParam(name: 'ENABLE_HOMEBREW', defaultValue: true, description: 'Allow Homebrew channel validation on macOS.')
        booleanParam(name: 'ENABLE_WINGET', defaultValue: false, description: 'Allow winget channel validation on Windows when configured.')
        booleanParam(name: 'ENABLE_APT', defaultValue: false, description: 'Allow apt channel validation on Linux when configured.')
        booleanParam(name: 'ENABLE_MSI', defaultValue: true, description: 'Allow Windows MSI validation when an MSI asset exists.')
        booleanParam(name: 'ALLOW_TAR_FALLBACK', defaultValue: true, description: 'Allow portable tarball fallback after higher-fidelity channels are unavailable.')
        booleanParam(name: 'EXECUTE_INSTALL', defaultValue: true, description: 'Actually download/install and run CLI smoke when the selected channel supports it.')
        booleanParam(name: 'FAIL_IF_NO_PASS', defaultValue: true, description: 'Fail the job when no platform install smoke passes.')
    }
    environment {
        VERIFY_ROOT = 'Release/install-verification'
    }
    stages {
        stage('Checkout') {
            steps {
                checkout scm
            }
        }
        stage('Dispatch GitHub Actions backend') {
            when { expression { return params.USE_GITHUB_ACTIONS_CLOUD } }
            steps {
                sh '''#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$VERIFY_ROOT/cloud"
if ! command -v gh >/dev/null 2>&1; then
  echo "gh is unavailable; cloud backend cannot be dispatched." | tee "$VERIFY_ROOT/cloud/backend-warning.txt"
  exit 0
fi
set +e
gh workflow run post-release-install-verify.yml \
  --repo "$GITHUB_REPOSITORY" \
  -f release_tag="$RELEASE_TAG" \
  -f release_version="$RELEASE_VERSION" \
  -f platform="$PLATFORM" \
  -f execute_install="$EXECUTE_INSTALL" \
  -f enable_homebrew="$ENABLE_HOMEBREW" \
  -f enable_winget="$ENABLE_WINGET" \
  -f enable_apt="$ENABLE_APT" \
  -f enable_msi="$ENABLE_MSI" \
  -f allow_tar_fallback="$ALLOW_TAR_FALLBACK"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
  echo "GitHub Actions dispatch failed with exit code $rc; local backend may run if allowed." | tee "$VERIFY_ROOT/cloud/backend-warning.txt"
else
  echo "GitHub Actions post-release install verification dispatched for $PLATFORM." | tee "$VERIFY_ROOT/cloud/backend-dispatched.txt"
fi
'''
            }
        }
        stage('Local backend verification') {
            when { expression { return params.ALLOW_LOCAL_BACKEND } }
            steps {
                sh '''#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$VERIFY_ROOT/local"
args=(
  --repo "$GITHUB_REPOSITORY"
  --tag "$RELEASE_TAG"
  --version "$RELEASE_VERSION"
  --platform "$PLATFORM"
  --output-dir "$VERIFY_ROOT/local"
  --cloud-backend local
)
[ "$ENABLE_HOMEBREW" = "true" ] && args+=(--enable-homebrew) || args+=(--no-enable-homebrew)
[ "$ENABLE_WINGET" = "true" ] && args+=(--enable-winget) || args+=(--no-enable-winget)
[ "$ENABLE_APT" = "true" ] && args+=(--enable-apt) || args+=(--no-enable-apt)
[ "$ENABLE_MSI" = "true" ] && args+=(--enable-msi) || args+=(--no-enable-msi)
[ "$ALLOW_TAR_FALLBACK" = "true" ] && args+=(--allow-tar-fallback) || args+=(--no-allow-tar-fallback)
[ "$EXECUTE_INSTALL" = "true" ] && args+=(--execute-install)
[ "$FAIL_IF_NO_PASS" = "true" ] && args+=(--fail-if-no-pass)
bash src/shell/release/08-post-release-install-verify.sh "${args[@]}"
'''
            }
        }
        stage('Release asset / MSI recheck') {
            steps {
                sh '''#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$VERIFY_ROOT/release-assets"
bash src/shell/release/07-recheck-release-assets-msi.sh \
  --repo "$GITHUB_REPOSITORY" \
  --tag "$RELEASE_TAG" \
  --output-dir "$VERIFY_ROOT/release-assets"
'''
            }
        }
    }
    post {
        always {
            archiveArtifacts artifacts: 'Release/install-verification/**', allowEmptyArchive: true
            script {
                if (fileExists("${env.VERIFY_ROOT}/cloud/backend-warning.txt")) {
                    unstable('GitHub Actions cloud backend was unavailable; see backend-warning.txt.')
                }
            }
        }
    }
}
