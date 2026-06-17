#!/usr/bin/env bash
set -euo pipefail

KANO_WIX_LIB_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
KANO_WIX_SCRIPT_DIR="$(cd -- "${KANO_WIX_LIB_DIR}/.." && pwd)"
KANO_WIX_ROOT_DIR="$(cd -- "${KANO_WIX_SCRIPT_DIR}/.." && pwd)"
KANO_WIX_REPO_ROOT="$(cd -- "${KANO_WIX_ROOT_DIR}/../.." && pwd)"

kano_wix_trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

kano_wix_convert_to_msi_product_version() {
  local canonical
  canonical="$(kano_wix_trim "$1")"
  if [[ ! "$canonical" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(\.[0-9]+)?(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$ ]]; then
    echo "Canonical VERSION must start with '<major>.<minor>.<patch>': $canonical" >&2
    exit 1
  fi

  local major="${BASH_REMATCH[1]}"
  local minor="${BASH_REMATCH[2]}"
  local patch="${BASH_REMATCH[3]}"

  if (( major > 255 )); then
    echo "MSI ProductVersion major field must be <= 255: $canonical" >&2
    exit 1
  fi
  if (( minor > 255 )); then
    echo "MSI ProductVersion minor field must be <= 255: $canonical" >&2
    exit 1
  fi
  if (( patch > 65535 )); then
    echo "MSI ProductVersion build field must be <= 65535: $canonical" >&2
    exit 1
  fi

  printf '%s.%s.%s' "$major" "$minor" "$patch"
}

kano_wix_to_windows_path() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -aw "$1"
    return 0
  fi
  printf '%s' "$1"
}

kano_wix_remove_directory_if_exists() {
  local path="$1"
  if [[ -e "$path" ]]; then
    rm -rf "$path"
  fi
}

kano_wix_make_unique_temp_subdir() {
  local parent_dir="$1"
  mkdir -p "$parent_dir"
  local unique_id
  unique_id="$(date +%s)-$$-$RANDOM"
  local temp_dir="${parent_dir}/${unique_id}"
  mkdir -p "$temp_dir"
  printf '%s' "$temp_dir"
}

kano_wix_resolve_wix_exe() {
  local wix_exe_override="${WIX_EXE:-}"
  if [[ -n "$wix_exe_override" && -f "$wix_exe_override" ]]; then
    printf '%s' "$wix_exe_override"
    return 0
  fi

  if [[ -n "${WIX:-}" ]]; then
    if [[ -f "$WIX" ]]; then
      printf '%s' "$WIX"
      return 0
    fi
    if [[ -f "$WIX/wix.exe" ]]; then
      printf '%s' "$WIX/wix.exe"
      return 0
    fi
    if [[ -f "$WIX/bin/wix.exe" ]]; then
      printf '%s' "$WIX/bin/wix.exe"
      return 0
    fi
  fi

  if command -v wix >/dev/null 2>&1; then
    command -v wix
    return 0
  fi

  local common_paths=(
    "/c/Program Files/WiX Toolset v6.0/bin/wix.exe"
    "/c/Program Files/WiX Toolset v5.0/bin/wix.exe"
    "${KANO_WIX_ROOT_DIR}/.tools/wix.exe"
  )
  local common_path
  for common_path in "${common_paths[@]}"; do
    if [[ -f "$common_path" ]]; then
      printf '%s' "$common_path"
      return 0
    fi
  done

  if command -v dotnet >/dev/null 2>&1; then
    mkdir -p "${KANO_WIX_ROOT_DIR}/.tools"
    dotnet tool install --tool-path "${KANO_WIX_ROOT_DIR}/.tools" wix --version 6.0.2 >/dev/null || true
    if [[ -f "${KANO_WIX_ROOT_DIR}/.tools/wix.exe" ]]; then
      printf '%s' "${KANO_WIX_ROOT_DIR}/.tools/wix.exe"
      return 0
    fi
  fi

  echo "WiX v6 CLI not found. Install WiX Toolset v6 or set WIX_EXE." >&2
  exit 1
}

kano_wix_ensure_extension() {
  local wix_exe="$1"
  local extension_id="$2"
  local extension_ref="${extension_id}/6.0.2"
  if "$wix_exe" extension list | grep -Fqi "$extension_id"; then
    return 0
  fi
  "$wix_exe" extension add -g "$extension_ref" >/dev/null
}

kano_wix_copy_dir_if_present() {
  local source_dir="$1"
  local dest_dir="$2"
  if [[ -d "$source_dir" ]]; then
    mkdir -p "$dest_dir"
    cp -a "$source_dir"/. "$dest_dir"/
  fi
}

kano_wix_directory_has_files() {
  local dir="$1"
  if [[ ! -d "$dir" ]]; then
    return 1
  fi
  find "$dir" -type f -print -quit | grep -q .
}

kano_wix_resolve_binary_source() {
  local repo_root="$1"
  local override="${KANO_WIX_BINARY_SOURCE:-}"

  if [[ -n "$override" ]]; then
    if [[ -f "$override" ]]; then
      printf '%s' "$override"
      return 0
    fi
    echo "KANO_WIX_BINARY_SOURCE does not exist: $override" >&2
    return 1
  fi

  local preferred_candidates=(
    "${repo_root}/src/cpp/out/bin/windows-ninja-msvc-pgo-use/release/kano-backlog.exe"
    "${repo_root}/src/cpp/out/bin/windows-ninja-msvc/release/kano-backlog.exe"
  )
  local candidate
  for candidate in "${preferred_candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done

  while IFS= read -r candidate; do
    if [[ -n "$candidate" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done < <(find "${repo_root}/src/cpp/out/bin" -type f -path '*/release/kano-backlog.exe' 2>/dev/null | sort)

  echo "Required release native binary is missing." >&2
  echo "Expected one of:" >&2
  printf '  %s\n' "${preferred_candidates[@]}" >&2
  echo "Hint: build the Windows CLI first, or set KANO_WIX_BINARY_SOURCE to an explicit release kano-backlog.exe path." >&2
  find "${repo_root}/src/cpp/out/bin" -maxdepth 4 -type f -name 'kano-backlog.exe' 2>/dev/null >&2 || true
  return 1
}

kano_wix_write_cmd_wrapper() {
  local wrapper_path="$1"
  local target_bat="$2"
  cat >"$wrapper_path" <<EOF
@echo off
setlocal
call "%~dp0${target_bat}" %*
exit /b %ERRORLEVEL%
EOF
}

kano_wix_stage_payload() {
  local payload_root="$1"

  kano_wix_remove_directory_if_exists "$payload_root"
  mkdir -p "$payload_root"

  local file_name source_path
  for file_name in VERSION README.md SKILL.md LICENSE CHANGELOG.md CONTRIBUTING.md REFERENCE.md TESTING.md VERSIONING.md; do
    source_path="${KANO_WIX_REPO_ROOT}/${file_name}"
    if [[ -f "$source_path" ]]; then
      cp "$source_path" "$payload_root/$file_name"
    fi
  done

  for file_name in VERSION README.md SKILL.md LICENSE; do
    if [[ ! -f "${payload_root}/${file_name}" ]]; then
      echo "Required payload file missing after staging: ${file_name}" >&2
      exit 1
    fi
  done

  local dir_name
  for dir_name in assets docs examples profiles references scripts templates typings; do
    kano_wix_copy_dir_if_present "${KANO_WIX_REPO_ROOT}/${dir_name}" "${payload_root}/${dir_name}"
  done
  kano_wix_copy_dir_if_present "${KANO_WIX_REPO_ROOT}/src/shell" "${payload_root}/src/shell"

  local binary_source
  if ! binary_source="$(kano_wix_resolve_binary_source "$KANO_WIX_REPO_ROOT")"; then
    exit 1
  fi

  mkdir -p "${payload_root}/bin"
  cp "$binary_source" "${payload_root}/bin/kano-backlog.exe"

  mkdir -p "${payload_root}/scripts"
  if [[ -f "${payload_root}/scripts/kob.bat" ]]; then
    kano_wix_write_cmd_wrapper "${payload_root}/scripts/kob.cmd" "kob.bat"
  fi
  if [[ -f "${payload_root}/scripts/kano-backlog.bat" ]]; then
    kano_wix_write_cmd_wrapper "${payload_root}/scripts/kano-backlog.cmd" "kano-backlog.bat"
  fi
  printf '%s\n' "$KANO_WIX_CANONICAL_VERSION" > "${payload_root}/package-version.txt"
}

kano_wix_bool_for_dir() {
  local dir="$1"
  if kano_wix_directory_has_files "$dir"; then
    printf '1'
  else
    printf '0'
  fi
}

kano_wix_prepare_context() {
  local clean_output="$1"

  KANO_WIX_UPGRADE_CODE="${KANO_WIX_UPGRADE_CODE:-FE2FB0F2-0844-4A7F-A98C-F10E6710D29D}"
  KANO_WIX_ARCHITECTURE="${KANO_WIX_ARCHITECTURE:-x64}"
  KANO_WIX_OUTPUT_DIR="${KANO_WIX_ROOT_DIR}/out"
  KANO_WIX_OUTPUT_NAME="kano-agent-backlog-skill.msi"
  KANO_WIX_PRODUCT_FILE="${KANO_WIX_ROOT_DIR}/code/Product.wxs"
  KANO_WIX_VERSION_FILE="${KANO_WIX_REPO_ROOT}/VERSION"

  if [[ ! -f "$KANO_WIX_PRODUCT_FILE" ]]; then
    echo "WiX entrypoint not found: $KANO_WIX_PRODUCT_FILE" >&2
    exit 1
  fi
  if [[ ! -f "$KANO_WIX_VERSION_FILE" ]]; then
    echo "Canonical VERSION file not found: $KANO_WIX_VERSION_FILE" >&2
    exit 1
  fi

  if [[ "$clean_output" == "1" ]]; then
    kano_wix_remove_directory_if_exists "$KANO_WIX_OUTPUT_DIR"
  fi

  KANO_WIX_OUTPUT_DIR="$(mkdir -p "$KANO_WIX_OUTPUT_DIR" && cd "$KANO_WIX_OUTPUT_DIR" && pwd)"
  KANO_WIX_PAYLOAD_ROOT="${KANO_WIX_OUTPUT_DIR}/payload"
  KANO_WIX_MSI_OUTPUT="${KANO_WIX_OUTPUT_DIR}/${KANO_WIX_OUTPUT_NAME}"
  KANO_WIX_PDB_OUTPUT="${KANO_WIX_OUTPUT_DIR}/${KANO_WIX_OUTPUT_NAME%.msi}.wixpdb"
  KANO_WIX_INTERMEDIATE_DIR="$(kano_wix_make_unique_temp_subdir "${KANO_WIX_OUTPUT_DIR}/_wix")"

  KANO_WIX_CANONICAL_VERSION="$(kano_wix_trim "${KANO_PROJECT_VERSION:-$(<"$KANO_WIX_VERSION_FILE")}")"
  if [[ -z "$KANO_WIX_CANONICAL_VERSION" ]]; then
    echo "Canonical VERSION is empty." >&2
    exit 1
  fi

  KANO_WIX_PRODUCT_VERSION="$(kano_wix_convert_to_msi_product_version "$KANO_WIX_CANONICAL_VERSION")"
  kano_wix_stage_payload "$KANO_WIX_PAYLOAD_ROOT"

  KANO_WIX_HAS_ASSETS="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/assets")"
  KANO_WIX_HAS_DOCS="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/docs")"
  KANO_WIX_HAS_EXAMPLES="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/examples")"
  KANO_WIX_HAS_PROFILES="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/profiles")"
  KANO_WIX_HAS_REFERENCES="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/references")"
  KANO_WIX_HAS_SRC_SHELL="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/src/shell")"
  KANO_WIX_HAS_TEMPLATES="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/templates")"
  KANO_WIX_HAS_TYPINGS="$(kano_wix_bool_for_dir "${KANO_WIX_PAYLOAD_ROOT}/typings")"
}

kano_wix_print_context() {
  echo "WiX build pipeline"
  echo "  Entrypoint : $KANO_WIX_PRODUCT_FILE"
  echo "  Output     : $KANO_WIX_MSI_OUTPUT"
  echo "  Payload    : $KANO_WIX_PAYLOAD_ROOT"
  echo "  VERSION    : $KANO_WIX_CANONICAL_VERSION -> MSI $KANO_WIX_PRODUCT_VERSION"
  echo "  Scope      : per-user only"
  echo "  InstallRoot: %USERPROFILE%\\.agents\\skills\\kano-agent-backlog-skill"
  echo "  UpgradeCode: $KANO_WIX_UPGRADE_CODE"
}

kano_wix_run_build() {
  local wix_exe_path
  wix_exe_path="$(kano_wix_resolve_wix_exe)"
  kano_wix_ensure_extension "$wix_exe_path" "WixToolset.Util.wixext"

  local payload_root_win product_file_win msi_output_win pdb_output_win intermediate_dir_win
  payload_root_win="$(kano_wix_to_windows_path "$KANO_WIX_PAYLOAD_ROOT")"
  product_file_win="$(kano_wix_to_windows_path "$KANO_WIX_PRODUCT_FILE")"
  msi_output_win="$(kano_wix_to_windows_path "$KANO_WIX_MSI_OUTPUT")"
  pdb_output_win="$(kano_wix_to_windows_path "$KANO_WIX_PDB_OUTPUT")"
  intermediate_dir_win="$(kano_wix_to_windows_path "$KANO_WIX_INTERMEDIATE_DIR")"

  set -x
  "$wix_exe_path" build "$product_file_win" \
    -ext WixToolset.Util.wixext \
    -arch "$KANO_WIX_ARCHITECTURE" \
    -intermediatefolder "$intermediate_dir_win" \
    -o "$msi_output_win" \
    -pdb "$pdb_output_win" \
    -d "ProductVersion=$KANO_WIX_PRODUCT_VERSION" \
    -d "UpgradeCode=$KANO_WIX_UPGRADE_CODE" \
    -d "HasAssets=$KANO_WIX_HAS_ASSETS" \
    -d "HasDocs=$KANO_WIX_HAS_DOCS" \
    -d "HasExamples=$KANO_WIX_HAS_EXAMPLES" \
    -d "HasProfiles=$KANO_WIX_HAS_PROFILES" \
    -d "HasReferences=$KANO_WIX_HAS_REFERENCES" \
    -d "HasSrcShell=$KANO_WIX_HAS_SRC_SHELL" \
    -d "HasTemplates=$KANO_WIX_HAS_TEMPLATES" \
    -d "HasTypings=$KANO_WIX_HAS_TYPINGS" \
    -d "PayloadRoot=$payload_root_win"
  set +x

  echo "MSI created: $KANO_WIX_MSI_OUTPUT"
}

kano_wix_sanitize_component() {
  local value="$1"
  value="${value//\\//}"
  value="${value//\//-}"
  value="$(printf '%s' "$value" | tr -c 'A-Za-z0-9._+-' '-')"
  value="${value##[-.]}"
  value="${value%%[-.]}"
  printf '%s' "${value:-unknown}"
}

kano_wix_normalize_branch() {
  local value="$1"
  case "$value" in
    ""|"__DEFAULT_BRANCH__"|"__BUILD_BRANCH__"|"__SOURCE_BRANCH__"|"**BUILD_BRANCH**")
      value="main"
      ;;
  esac
  value="${value#refs/heads/}"
  value="${value#origin/}"
  value="${value#*/}"
  kano_wix_sanitize_component "$value"
}

kano_wix_public_msi_name() {
  local platform="${KANO_PLATFORM:-windows-x64}"
  local version="${KANO_PROJECT_VERSION:-$KANO_WIX_CANONICAL_VERSION}"
  local branch="${KANO_SELECTED_BRANCH:-${SOURCE_BRANCH:-${BUILD_BRANCH:-${BRANCH_NAME:-main}}}}"
  local config="${KANO_BUILD_CONFIG:-Release}"
  local target="${KANO_BUILD_TARGET:-cli}"
  printf '%s-%s-%s-%s-%s.msi' \
    "$(kano_wix_sanitize_component "$platform")" \
    "$(kano_wix_normalize_branch "$branch")" \
    "$(kano_wix_sanitize_component "$version")" \
    "$(kano_wix_sanitize_component "$config")" \
    "$(kano_wix_sanitize_component "$target")"
}

kano_wix_publish_installer_output() {
  local output_root="${KANO_INSTALLER_OUTPUT_ROOT:-}"
  if [[ -z "$output_root" ]]; then
    return 0
  fi
  if [[ ! -f "$KANO_WIX_MSI_OUTPUT" ]]; then
    echo "MSI output missing: $KANO_WIX_MSI_OUTPUT" >&2
    exit 1
  fi
  mkdir -p "$output_root"
  local target="${output_root}/$(kano_wix_public_msi_name)"
  cp "$KANO_WIX_MSI_OUTPUT" "$target"
  echo "Installer staged: $target"
}
