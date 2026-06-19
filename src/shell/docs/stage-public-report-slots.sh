#!/usr/bin/env bash
set -euo pipefail

# Stage public report slots for GitHub Pages.
#
# Preferred CI path:
#   KANO_PUBLIC_REPORT_SOURCE_DIR=/path/to/static/report-site \
#     bash src/shell/docs/stage-public-report-slots.sh _ws/build
#
# Backfill/rescue path:
#   KANO_PUBLIC_REPORT_SOURCE_URL=https://jenkins/.../Reports/ \
#   KANO_PUBLIC_REPORT_SOURCE_CURL_USER=...
#   KANO_PUBLIC_REPORT_SOURCE_CURL_PASSWORD=...
#     bash src/shell/docs/stage-public-report-slots.sh _ws/build

BUILD_DIR="${1:-${KANO_DOCS_BUILD_DIR:-_ws/build}}"
PUBLIC_ROOT="${KANO_PUBLIC_REPORT_ROOT:-reports/latest}"
SOURCE_DIR="${KANO_PUBLIC_REPORT_SOURCE_DIR:-}"
SOURCE_URL="${KANO_PUBLIC_REPORT_SOURCE_URL:-}"

normalize_public_root() {
  local value="${1:-reports/latest}"
  value="${value//\\//}"
  value="${value#/}"
  value="${value%/}"
  if [[ -z "$value" || "$value" == /* || "$value" == *".."* ]]; then
    printf '%s\n' "reports/latest"
  else
    printf '%s\n' "$value"
  fi
}

resolve_path() {
  local raw="${1:?path is required}"
  raw="${raw//\\//}"
  if command -v cygpath >/dev/null 2>&1 && [[ "$raw" =~ ^[A-Za-z]:/ ]]; then
    cygpath -u "$raw"
    return 0
  fi
  if [[ "$raw" == /* ]]; then
    printf '%s\n' "$raw"
  else
    printf '%s/%s\n' "$(pwd -P)" "${raw#./}"
  fi
}

curl_fetch() {
  local url="${1:?url is required}"
  local output="${2:?output is required}"
  local -a args=(-fsSL --retry 3 --retry-delay 1)
  if [[ -n "${KANO_PUBLIC_REPORT_SOURCE_AUTH_HEADER:-}" ]]; then
    args+=(-H "Authorization: ${KANO_PUBLIC_REPORT_SOURCE_AUTH_HEADER}")
  elif [[ -n "${KANO_PUBLIC_REPORT_SOURCE_CURL_USER:-}" || -n "${KANO_PUBLIC_REPORT_SOURCE_CURL_PASSWORD:-}" ]]; then
    args+=(-u "${KANO_PUBLIC_REPORT_SOURCE_CURL_USER:-}:${KANO_PUBLIC_REPORT_SOURCE_CURL_PASSWORD:-}")
  fi
  mkdir -p "$(dirname "$output")"
  curl "${args[@]}" "$url" -o "$output"
}

extract_local_refs() {
  local html_file="${1:?html file is required}"
  grep -Eo '(href|src)="[^"]+"' "$html_file" 2>/dev/null |
    sed -E 's/^(href|src)="([^"]+)"/\2/' |
    sed -E 's/[?#].*$//' |
    grep -v '^$' |
    grep -Ev '^(#|/|[a-zA-Z][a-zA-Z0-9+.-]*:|\\.\\.)' |
    sort -u || true
}

download_report_url() {
  local base_url="${1:?source url is required}"
  local target_dir="${2:?target dir is required}"
  local queue_file seen_file rel next_file joined_ref
  base_url="${base_url%/}"
  mkdir -p "$target_dir"
  queue_file="$target_dir/.download-queue"
  seen_file="$target_dir/.download-seen"
  : > "$queue_file"
  : > "$seen_file"

  printf '%s\n' "index.html" >> "$queue_file"

  next_pending_ref() {
    local candidate
    while IFS= read -r candidate; do
      [[ -n "$candidate" ]] || continue
      grep -Fxq "$candidate" "$seen_file" || {
        printf '%s\n' "$candidate"
        return 0
      }
    done < "$queue_file"
    return 1
  }

  join_relative_ref() {
    local parent_ref="${1:?parent ref is required}"
    local child_ref="${2:?child ref is required}"
    local parent_dir
    parent_dir="$(dirname "$parent_ref")"
    if [[ "$parent_dir" == "." ]]; then
      printf '%s\n' "$child_ref"
    else
      printf '%s/%s\n' "$parent_dir" "$child_ref"
    fi
  }

  while rel="$(next_pending_ref)"; do
    printf '%s\n' "$rel" >> "$seen_file"
    case "$rel" in
      /*|*://*|*..*) continue ;;
    esac
    curl_fetch "$base_url/$rel" "$target_dir/$rel"
    if [[ "$rel" == *.html ]]; then
      while IFS= read -r next_file; do
        [[ -n "$next_file" ]] || continue
        joined_ref="$(join_relative_ref "$rel" "$next_file")"
        grep -Fxq "$joined_ref" "$seen_file" || printf '%s\n' "$joined_ref" >> "$queue_file"
      done < <(extract_local_refs "$target_dir/$rel")
    fi
  done

  rm -f "$queue_file" "$seen_file"
}

write_slot_placeholder() {
  local slot_dir="${1:?slot dir is required}"
  local title="${2:?title is required}"
  mkdir -p "$slot_dir"
  cat > "$slot_dir/index.html" <<HTML
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${title}</title>
</head>
<body>
  <main>
    <h1>${title}</h1>
    <p>No publishable report HTML was staged for this slot.</p>
  </main>
</body>
</html>
HTML
}

PUBLIC_ROOT="$(normalize_public_root "$PUBLIC_ROOT")"
BUILD_DIR="$(resolve_path "$BUILD_DIR")"
TARGET_ROOT="$BUILD_DIR/staged/$PUBLIC_ROOT"
TMP_DIR=""

cleanup() {
  if [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup EXIT

if [[ -z "$SOURCE_DIR" && -z "$SOURCE_URL" ]]; then
  echo "No KANO_PUBLIC_REPORT_SOURCE_DIR or KANO_PUBLIC_REPORT_SOURCE_URL set; skipping public report staging."
  exit 0
fi

if [[ -n "$SOURCE_URL" ]]; then
  TMP_DIR="$(mktemp -d)"
  echo "Downloading public report source from URL into temporary staging."
  download_report_url "$SOURCE_URL" "$TMP_DIR/source"
  SOURCE_DIR="$TMP_DIR/source"
else
  SOURCE_DIR="$(resolve_path "$SOURCE_DIR")"
fi

if [[ ! -f "$SOURCE_DIR/index.html" ]]; then
  echo "ERROR: report source does not contain index.html: $SOURCE_DIR" >&2
  exit 1
fi

rm -rf "$TARGET_ROOT/test-report" "$TARGET_ROOT/coverage-report"
mkdir -p "$TARGET_ROOT/test-report" "$TARGET_ROOT/coverage-report"
cp -a "$SOURCE_DIR"/. "$TARGET_ROOT/test-report"/

coverage_index="$(
  find "$SOURCE_DIR" -path '*/coverage-reports/*/report-html/index.html' -type f 2>/dev/null |
    sort |
    head -n 1 || true
)"
if [[ -n "$coverage_index" ]]; then
  cp -a "$(dirname "$coverage_index")"/. "$TARGET_ROOT/coverage-report"/
else
  write_slot_placeholder "$TARGET_ROOT/coverage-report" "Latest Coverage Report"
fi

cat > "$TARGET_ROOT/public-report-slots.json" <<JSON
{
  "schemaVersion": 1,
  "kind": "kano-public-report-slots",
  "publicRoot": "$PUBLIC_ROOT",
  "source": {
    "kind": "$(if [[ -n "$SOURCE_URL" ]]; then printf 'jenkins-html-publisher-url'; else printf 'directory'; fi)",
    "publicUrl": "$(printf '%s' "${KANO_PUBLIC_REPORT_SOURCE_PUBLIC_URL:-}")"
  },
  "slots": [
    {"name": "test-report", "path": "$PUBLIC_ROOT/test-report", "status": "populated"},
    {"name": "coverage-report", "path": "$PUBLIC_ROOT/coverage-report", "status": "$(if [[ -n "$coverage_index" ]]; then printf 'populated'; else printf 'reserved'; fi)"}
  ]
}
JSON

echo "Public report slots staged under $TARGET_ROOT"
