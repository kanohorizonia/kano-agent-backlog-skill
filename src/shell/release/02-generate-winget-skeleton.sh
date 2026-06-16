#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 0 ] && [ $# -ne 5 ]; then
  echo "Usage: $0 [<repo-root> <tag-name> <repo-slug> <artifact-dir> <output-dir>]" >&2
  exit 1
fi

REPO_ROOT="${1:-$(pwd)}"
VERSION_TEXT="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
TAG_NAME="${2:-${KANO_RELEASE_TAG:-v${VERSION_TEXT}}}"
REPO_SLUG="${3:-${KANO_GITHUB_REPOSITORY:-kanohorizonia/kano-agent-backlog-skill}}"
ARTIFACT_DIR="${4:-${KANO_PACKAGE_ROOT:-artifacts/packages}}"
OUTPUT_DIR="${5:-${KANO_PACKAGE_MANAGER_RECIPE_ROOT:-Release/package-managers}/winget}"
PACKAGE_ID="${KANO_WINGET_PACKAGE_ID:-KanoHorizonia.KanoBacklog}"
PACKAGE_IDENTIFIER="${KANO_WINGET_PACKAGE_IDENTIFIER:-$PACKAGE_ID}"
ASSET_BASE_URL="${KANO_RELEASE_ASSET_BASE_URL:-https://github.com/${REPO_SLUG}/releases/download/${TAG_NAME}}"

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

find_windows_artifact() {
  local root found
  for root in "$ARTIFACT_DIR" "$ARTIFACT_DIR/packages" artifacts artifacts/packages Release Release/artifacts; do
    [ -d "$root" ] || continue
    found="$(find "$root" -type f \( \
      -iname '*windows-x64*.msi' -o -iname '*win-x64*.msi' -o \
      -iname '*windows-x64*.zip' -o -iname '*win-x64*.zip' -o \
      -iname '*windows-x64*.tar.gz' -o -iname '*win-x64*.tar.gz' \
    \) | sort | head -n 1 || true)"
    if [ -n "$found" ]; then
      printf '%s\n' "$found"
      return 0
    fi
  done
}

mkdir -p "$OUTPUT_DIR"
ARTIFACT_PATH="$(find_windows_artifact || true)"

if [ -z "$ARTIFACT_PATH" ]; then
  cat > "$OUTPUT_DIR/README.md" <<EOF
# ${PACKAGE_IDENTIFIER} winget preparation

No Windows installer or archive artifact was found.

winget publication requires a stable GitHub Release asset URL and SHA256 value.
If the final KOB release uses the current root-flat CLI tarball only, decide
whether winget should package an MSI, ZIP installer, or app installer before
submitting the external microsoft/winget-pkgs PR.
EOF
  cat > "$OUTPUT_DIR/${PACKAGE_IDENTIFIER}.winget-intent.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "kano-winget-release-intent",
  "packageIdentifier": "${PACKAGE_IDENTIFIER}",
  "version": "${VERSION_TEXT}",
  "tag": "${TAG_NAME}",
  "status": "missing-artifact",
  "requiresExternalPr": true,
  "upstreamRepository": "microsoft/winget-pkgs"
}
EOF
  echo "$OUTPUT_DIR"
  exit 0
fi

ARTIFACT_FILE="$(basename "$ARTIFACT_PATH")"
ARTIFACT_SHA256="$(calc_sha256 "$ARTIFACT_PATH")"
ARTIFACT_URL="${ASSET_BASE_URL%/}/${ARTIFACT_FILE}"

cat > "$OUTPUT_DIR/README.md" <<EOF
# ${PACKAGE_IDENTIFIER} winget preparation

This directory is a review payload for the external winget PR.

- asset: ${ARTIFACT_URL}
- sha256: ${ARTIFACT_SHA256}
- external PR target: microsoft/winget-pkgs

Only submit the PR after the GitHub Release asset URL is immutable.
EOF

cat > "$OUTPUT_DIR/${PACKAGE_IDENTIFIER}.winget-intent.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "kano-winget-release-intent",
  "packageIdentifier": "${PACKAGE_IDENTIFIER}",
  "version": "${VERSION_TEXT}",
  "tag": "${TAG_NAME}",
  "status": "ready-for-external-pr-review",
  "asset": {
    "fileName": "${ARTIFACT_FILE}",
    "url": "${ARTIFACT_URL}",
    "sha256": "${ARTIFACT_SHA256}"
  },
  "requiresExternalPr": true,
  "upstreamRepository": "microsoft/winget-pkgs"
}
EOF

echo "$OUTPUT_DIR"
