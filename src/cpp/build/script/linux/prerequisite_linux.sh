#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "Unsupported Linux package manager for this script. Please install cmake, ninja-build, gcc/g++, clang, and git manually." >&2
  exit 1
fi

SUDO=""
if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "This script needs root privileges (run as root or install sudo)." >&2
    exit 1
  fi
fi

${SUDO} apt-get update
${SUDO} DEBIAN_FRONTEND=noninteractive apt-get install -y \
  ca-certificates \
  cmake \
  ninja-build \
  gcc \
  g++ \
  clang \
  git \
  pkg-config

echo "Linux prerequisites setup complete."
