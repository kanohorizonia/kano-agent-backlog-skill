#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

LATEST=0
DETAILED=0

usage() {
  cat <<'EOF'
Usage: list-tags.sh [--latest] [--detailed]

List git tags for this repository.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --latest) LATEST=1; shift ;;
    --detailed) DETAILED=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unsupported argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if ! git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Not inside a git repository: $REPO_ROOT" >&2
  exit 1
fi

if [[ "$DETAILED" == "1" ]]; then
  FORMAT='%(refname:short)|%(creatordate:iso8601)|%(subject)'
else
  FORMAT='%(refname:short)'
fi

mapfile -t TAGS < <(git -C "$REPO_ROOT" for-each-ref --sort=-creatordate --format="$FORMAT" refs/tags)

if [[ ${#TAGS[@]} -eq 0 ]]; then
  echo "No tags found."
  exit 0
fi

if [[ "$LATEST" == "1" ]]; then
  printf '%s\n' "${TAGS[0]}"
  exit 0
fi

printf '%s\n' "${TAGS[@]}"
