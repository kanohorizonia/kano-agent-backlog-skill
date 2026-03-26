#!/usr/bin/env bash
set -euo pipefail

if ! command -v xcode-select >/dev/null 2>&1; then
  echo "xcode-select not found. Install Xcode Command Line Tools first." >&2
  exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Installing Xcode Command Line Tools..."
  xcode-select --install || true
  echo "Please complete the Command Line Tools installation, then re-run this script." >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required. Install from https://brew.sh and re-run." >&2
  exit 1
fi

if ! brew update; then
  echo "Warning: brew update failed; continuing with no-auto-update install attempt." >&2
fi

HOMEBREW_NO_AUTO_UPDATE=1 brew upgrade cmake ninja git llvm || true
HOMEBREW_NO_AUTO_UPDATE=1 brew install cmake ninja git llvm

echo "macOS prerequisites setup complete (cmake, ninja, git, llvm)."
