#!/usr/bin/env bash
set -euo pipefail

if [ $# -gt 3 ]; then
  echo "Usage: $0 [<repo-root> <artifact-root> <output-root>]" >&2
  exit 1
fi

REPO_ROOT="${1:-$(pwd)}"
ARTIFACT_ROOT="${2:-artifacts}"
OUTPUT_ROOT="${3:-${KANO_PACKAGE_MANAGER_RECIPE_ROOT:-Release/package-managers}}"
VERSION_TEXT="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
TAG_NAME="${KANO_RELEASE_TAG:-v${VERSION_TEXT}}"
REPO_SLUG="${KANO_GITHUB_REPOSITORY:-kanohorizonia/kano-agent-backlog-skill}"
ASSET_BASE_URL="${KANO_RELEASE_ASSET_BASE_URL:-https://github.com/${REPO_SLUG}/releases/download/${TAG_NAME}}"
OUTPUT_DIR="${OUTPUT_ROOT%/}/apt"
PACKAGE_NAME="${KANO_APT_PACKAGE_NAME:-kano-backlog}"
PUBLIC_STRIP_PREFIXES="${KANO_PUBLIC_RELEASE_ASSET_STRIP_PREFIXES:-}"

calc_sha256() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
    return 0
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print $1}'
    return 0
  fi
  if command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$path" | awk '{print $NF}'
    return 0
  fi
  echo "ERROR: sha256sum, shasum, or openssl is required to hash $path" >&2
  exit 115
}

find_linux_archive() {
  local root found
  for root in "$ARTIFACT_ROOT" "$ARTIFACT_ROOT/packages" artifacts artifacts/packages Release Release/artifacts; do
    [ -d "$root" ] || continue
    found="$(find "$root" -type f \( \
      -iname '*linux-x64*.tar.gz' -o -iname '*linux-amd64*.tar.gz' -o \
      -iname '*linux-x64*.tgz' -o -iname '*linux-amd64*.tgz' -o \
      -iname '*linux*.tar.gz' -o -iname '*linux*.tgz' \
    \) | sort | head -n 1 || true)"
    if [ -n "$found" ]; then
      printf '%s\n' "$found"
      return 0
    fi
  done
}

public_asset_name() {
  local name="$1"
  local prefix
  IFS=',' read -r -a prefixes <<< "$PUBLIC_STRIP_PREFIXES"
  for prefix in "${prefixes[@]}"; do
    prefix="${prefix#"${prefix%%[![:space:]]*}"}"
    prefix="${prefix%"${prefix##*[![:space:]]}"}"
    [ -n "$prefix" ] || continue
    case "${name,,}" in
      "${prefix,,}"*)
        name="${name:${#prefix}}"
        while [[ "$name" == [-_.]* ]]; do
          name="${name:1}"
        done
        ;;
    esac
  done
  printf '%s\n' "$name"
}

mkdir -p "$OUTPUT_DIR"
LINUX_ARCHIVE="$(find_linux_archive || true)"

if [ -z "$LINUX_ARCHIVE" ]; then
  cat > "$OUTPUT_DIR/README.md" <<EOF
# ${PACKAGE_NAME} apt preparation

No Linux release archive was found.

apt publication needs a .deb package plus repository metadata under pool/ and
dists/. The current KOB release packaging prepares root-flat native CLI archives;
Debian package authoring must wrap that archive before non-dry-run apt publish.
EOF
  cat > "$OUTPUT_DIR/${PACKAGE_NAME}.apt-recipe.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "kano-apt-release-intent",
  "packageName": "${PACKAGE_NAME}",
  "version": "${VERSION_TEXT}",
  "tag": "${TAG_NAME}",
  "status": "missing-artifact",
  "requiresDebAuthoring": true,
  "targetBranch": "main"
}
EOF
  echo "$OUTPUT_DIR"
  exit 0
fi

ARCHIVE_FILE="$(public_asset_name "$(basename "$LINUX_ARCHIVE")")"
ARCHIVE_SHA256="$(calc_sha256 "$LINUX_ARCHIVE")"
ARCHIVE_URL="${ASSET_BASE_URL%/}/${ARCHIVE_FILE}"

cat > "$OUTPUT_DIR/README.md" <<EOF
# ${PACKAGE_NAME} apt preparation

This directory is generated as the apt package-manager lane input.

- source archive: ${ARCHIVE_URL}
- sha256: ${ARCHIVE_SHA256}

The current Linux release artifact is a native CLI tarball. Debian package
authoring must wrap it into a .deb and generate signed pool/ and dists/
metadata before non-dry-run apt publish.
EOF

cat > "$OUTPUT_DIR/${PACKAGE_NAME}.apt-recipe.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "kano-apt-release-intent",
  "packageName": "${PACKAGE_NAME}",
  "version": "${VERSION_TEXT}",
  "tag": "${TAG_NAME}",
  "status": "requires-deb-authoring",
  "sourceArchive": {
    "fileName": "${ARCHIVE_FILE}",
    "url": "${ARCHIVE_URL}",
    "sha256": "${ARCHIVE_SHA256}"
  },
  "publish": {
    "repositoryUrlEnv": "KANO_APT_REPO_URL",
    "targetBranchEnv": "KANO_APT_TARGET_BRANCH",
    "credentialEnv": "KANO_APT_REPO_TOKEN"
  },
  "debian": {
    "package": "${PACKAGE_NAME}",
    "section": "utils",
    "priority": "optional",
    "architecture": "amd64",
    "summary": "Kano Agent Backlog native CLI",
    "requiresDebAuthoring": true
  }
}
EOF

cat > "$OUTPUT_DIR/debian-control.stub" <<EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION_TEXT}
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Kano Horizonia <jenkins@kanohorizonia.local>
Description: Kano Agent Backlog native CLI
 This is a dry-run control stub. Fill Depends, install layout, maintainer
 scripts, signing, and repository metadata before enabling non-dry-run apt publish.
EOF

echo "$OUTPUT_DIR"
