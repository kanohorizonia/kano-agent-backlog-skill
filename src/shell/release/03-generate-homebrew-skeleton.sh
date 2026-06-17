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
OUTPUT_DIR="${5:-${KANO_PACKAGE_MANAGER_RECIPE_ROOT:-Release/package-managers}/homebrew}"
FORMULA_NAME="${KANO_HOMEBREW_FORMULA_NAME:-kano-backlog}"
ASSET_BASE_URL="${KANO_RELEASE_ASSET_BASE_URL:-https://github.com/${REPO_SLUG}/releases/download/${TAG_NAME}}"
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

find_first() {
  local root pattern found
  for root in "$ARTIFACT_DIR" "$ARTIFACT_DIR/packages" artifacts artifacts/packages Release Release/artifacts; do
    [ -d "$root" ] || continue
    for pattern in "$@"; do
      found="$(find "$root" -type f -iname "$pattern" | sort | head -n 1 || true)"
      if [ -n "$found" ]; then
        printf '%s\n' "$found"
        return 0
      fi
    done
  done
}

formula_class_name() {
  local sanitized part first rest result
  sanitized="$(printf '%s' "$FORMULA_NAME" | sed -E 's/[^A-Za-z0-9]+/ /g')"
  result=""
  for part in $sanitized; do
    [ -n "$part" ] || continue
    first="$(printf '%s' "${part:0:1}" | tr '[:lower:]' '[:upper:]')"
    rest="${part:1}"
    result="${result}${first}${rest}"
  done
  printf '%s\n' "${result:-KanoBacklog}"
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
ARM64_PATH="$(find_first '*macos-arm64*.tar.gz' '*mac-arm64*.tar.gz' '*darwin-arm64*.tar.gz' || true)"
X64_PATH="$(find_first '*macos-x64*.tar.gz' '*macos-amd64*.tar.gz' '*mac-x64*.tar.gz' '*darwin-x64*.tar.gz' || true)"
GENERIC_PATH="$(find_first '*macos*.tar.gz' '*darwin*.tar.gz' '*mac*.tar.gz' || true)"
FORMULA_FILE="$OUTPUT_DIR/${FORMULA_NAME}.rb"
CLASS_NAME="$(formula_class_name)"

if [ -z "$ARM64_PATH" ] && [ -z "$X64_PATH" ] && [ -z "$GENERIC_PATH" ]; then
  cat > "$OUTPUT_DIR/README.md" <<EOF
# ${FORMULA_NAME} Homebrew preparation

No macOS release archive was found.

Homebrew formula generation needs a stable macOS GitHub Release asset URL and
SHA256 value. If Kano owns a tap, commit the generated formula to that tap's
main branch after release assets are final. Homebrew core requires a PR.
EOF
  cat > "$OUTPUT_DIR/${FORMULA_NAME}.homebrew-intent.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "kano-homebrew-release-intent",
  "formulaName": "${FORMULA_NAME}",
  "version": "${VERSION_TEXT}",
  "tag": "${TAG_NAME}",
  "status": "missing-artifact",
  "targetBranch": "main"
}
EOF
  echo "$OUTPUT_DIR"
  exit 0
fi

write_formula_header() {
  cat > "$FORMULA_FILE" <<EOF
class ${CLASS_NAME} < Formula
  desc "Kano Agent Backlog native CLI"
  homepage "https://github.com/${REPO_SLUG}"
  version "${VERSION_TEXT}"
EOF
}

write_url_block() {
  local path="$1"
  local file sha
  file="$(public_asset_name "$(basename "$path")")"
  sha="$(calc_sha256 "$path")"
  cat >> "$FORMULA_FILE" <<EOF
  url "${ASSET_BASE_URL%/}/${file}"
  sha256 "${sha}"
EOF
}

write_arch_url_block() {
  local arm_file x64_file arm_sha x64_sha
  arm_file="$(public_asset_name "$(basename "$ARM64_PATH")")"
  x64_file="$(public_asset_name "$(basename "$X64_PATH")")"
  arm_sha="$(calc_sha256 "$ARM64_PATH")"
  x64_sha="$(calc_sha256 "$X64_PATH")"
  cat >> "$FORMULA_FILE" <<EOF

  on_macos do
    if Hardware::CPU.arm?
      url "${ASSET_BASE_URL%/}/${arm_file}"
      sha256 "${arm_sha}"
    else
      url "${ASSET_BASE_URL%/}/${x64_file}"
      sha256 "${x64_sha}"
    end
  end
EOF
}

write_install_block() {
  cat >> "$FORMULA_FILE" <<'EOF'

  def install
    libexec.install Dir["*"]
    payload = libexec/"kano-agent-backlog-skill"
    bin.install_symlink payload/"scripts/kob" => "kob"
    bin.install_symlink payload/"scripts/kano-backlog" => "kano-backlog"
  end

  test do
    system "#{bin}/kano-backlog", "version"
  end
end
EOF
}

write_formula_header
if [ -n "$ARM64_PATH" ] && [ -n "$X64_PATH" ]; then
  write_arch_url_block
elif [ -n "$GENERIC_PATH" ]; then
  write_url_block "$GENERIC_PATH"
else
  write_url_block "${ARM64_PATH:-$X64_PATH}"
fi
write_install_block

cat > "$OUTPUT_DIR/README.md" <<EOF
# ${FORMULA_NAME} Homebrew preparation

Generated formula: ${FORMULA_NAME}.rb

If Kano owns a tap, commit this formula to the tap's main branch after release
assets are final. Homebrew core requires a PR.
EOF

echo "$OUTPUT_DIR"
