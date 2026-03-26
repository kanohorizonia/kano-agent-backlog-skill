#!/usr/bin/env bash
set -euo pipefail

_kob_trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

_kob_default_unknown() {
  local value="$1"
  if [[ -z "$value" ]]; then
    printf '%s' "unknown"
    return
  fi
  printf '%s' "$value"
}

kob_cpp_root() {
  if [[ -n "${KOB_CPP_ROOT:-}" ]]; then
    printf '%s' "$KOB_CPP_ROOT"
    return
  fi
  if [[ -n "${KABSD_CPP_ROOT:-}" ]]; then
    printf '%s' "$KABSD_CPP_ROOT"
    return
  fi
  pwd
}

kob_workspace_root() {
  local cpp_root
  cpp_root="$(kob_cpp_root)"
  (cd "$cpp_root/../.." && pwd)
}

_kob_extract_toml_section_value() {
  local file_path="$1"
  local section_name="$2"
  local key_name="$3"
  awk -v section="$section_name" -v key="$key_name" '
    function trim(s) {
      sub(/^[[:space:]]+/, "", s)
      sub(/[[:space:]]+$/, "", s)
      return s
    }
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*$/ { next }
    /^[[:space:]]*\[/ {
      current = $0
      sub(/^[[:space:]]*\[/, "", current)
      sub(/\][[:space:]]*$/, "", current)
      current = trim(current)
      in_section = (current == section)
      next
    }
    in_section {
      line = $0
      sub(/[[:space:]]+#.*$/, "", line)
      if (line ~ "^[[:space:]]*" key "[[:space:]]*=") {
        sub("^[[:space:]]*" key "[[:space:]]*=[[:space:]]*", "", line)
        line = trim(line)
        if (line ~ /^".*"$/) {
          sub(/^"/, "", line)
          sub(/"$/, "", line)
        }
        if (line ~ /^'"'"'.*'"'"'$/) {
          sub(/^'"'"'/, "", line)
          sub(/'"'"'$/, "", line)
        }
        print line
      }
    }
  ' "$file_path" | tail -n 1
}

kob_resolve_self_config_value() {
  local key_name="$1"
  local workspace_root
  local home_dir="${HOME:-}"
  local value=""
  local file_path=""
  workspace_root="$(kob_workspace_root)"

  for file_path in \
    "$workspace_root/assets/kob_config.toml" \
    "$home_dir/.kano/kob_config.toml" \
    "$workspace_root/.kano/kob_config.toml"; do
    if [[ -f "$file_path" ]]; then
      local candidate=""
      candidate="$(_kob_extract_toml_section_value "$file_path" "self" "$key_name")"
      if [[ -n "$candidate" ]]; then
        value="$candidate"
      fi
    fi
  done

  printf '%s' "$value"
}

kob_apply_self_build_config() {
  if [[ -z "${KOB_COMPILER_LAUNCHER:-}" ]]; then
    local configured_launcher=""
    configured_launcher="$(kob_resolve_self_config_value "compiler_launcher")"
    if [[ -n "$configured_launcher" ]]; then
      export KOB_COMPILER_LAUNCHER="$configured_launcher"
    fi
  fi
}

kob_collect_build_metadata() {
  local root
  local version_file
  local version="unknown"
  local branch="unknown"
  local hash_short="unknown"
  local hash_full="unknown"
  local dirty="unknown"
  local host_name
  local platform
  root="$(kob_workspace_root)"
  version_file="$root/VERSION"

  if [[ -f "$version_file" ]]; then
    version="$(_kob_trim "$(<"$version_file")")"
  fi

  host_name="${KOB_BUILD_HOST_NAME:-${HOSTNAME:-$(hostname 2>/dev/null || printf 'unknown')}}"
  platform="${KOB_BUILD_PLATFORM:-$(uname -s 2>/dev/null || printf 'unknown')-$(uname -m 2>/dev/null || printf 'unknown')}"

  if command -v git >/dev/null 2>&1 && (cd "$root" && git rev-parse --is-inside-work-tree >/dev/null 2>&1); then
    branch="$( (cd "$root" && git symbolic-ref --short HEAD 2>/dev/null) || true )"
    hash_short="$( (cd "$root" && git rev-parse --short HEAD 2>/dev/null) || true )"
    hash_full="$( (cd "$root" && git rev-parse HEAD 2>/dev/null) || true )"
    if [[ -n "$( (cd "$root" && git status --porcelain 2>/dev/null) || true )" ]]; then
      dirty="true"
    else
      dirty="false"
    fi
  fi

  export KOB_BUILD_VERSION="$(_kob_default_unknown "$version")"
  export KOB_BUILD_BRANCH="$(_kob_default_unknown "$(_kob_trim "$branch")")"
  export KOB_BUILD_REVISION_HASH_SHORT="$(_kob_default_unknown "$(_kob_trim "$hash_short")")"
  export KOB_BUILD_REVISION_HASH="$(_kob_default_unknown "$(_kob_trim "$hash_full")")"
  export KOB_BUILD_DIRTY="$(_kob_default_unknown "$(_kob_trim "$dirty")")"
  export KOB_BUILD_HOST_NAME="$(_kob_default_unknown "$(_kob_trim "$host_name")")"
  export KOB_BUILD_PLATFORM="$(_kob_default_unknown "$(_kob_trim "$platform")")"
}
