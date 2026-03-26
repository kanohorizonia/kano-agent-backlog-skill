#!/usr/bin/env bash
set -euo pipefail

if ! command -v powershell >/dev/null 2>&1; then
  echo "powershell is required." >&2
  exit 1
fi

powershell -NoProfile -ExecutionPolicy Bypass -Command - <<'POWERSHELL'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Ensure-WingetPackage {
  param([string]$Id)

  $noUpgradeCode = -1978335189
  $installed = winget list --id $Id --exact --accept-source-agreements 2>$null
  if ($LASTEXITCODE -ne 0) {
    winget install --id $Id --exact --source winget --accept-source-agreements --accept-package-agreements --silent --disable-interactivity
    if ($LASTEXITCODE -ne 0) {
      throw "winget install failed for $Id with exit code $LASTEXITCODE"
    }
    return
  }

  winget upgrade --id $Id --exact --source winget --accept-source-agreements --accept-package-agreements --silent --disable-interactivity
  if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $noUpgradeCode) {
    throw "winget upgrade failed for $Id with exit code $LASTEXITCODE"
  }
}

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
  throw 'winget is required to auto-install prerequisites. Please install App Installer from Microsoft Store.'
}

Ensure-WingetPackage -Id 'Kitware.CMake'
Ensure-WingetPackage -Id 'Ninja-build.Ninja'

Write-Host 'Windows prerequisites setup complete.'
POWERSHELL
