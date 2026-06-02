$ErrorActionPreference = "Stop"

function Get-BashPath {
    $bash = Get-Command "bash.exe" -ErrorAction SilentlyContinue
    if ($bash) { return $bash.Source }

    $bash = Get-Command "bash" -ErrorAction SilentlyContinue
    if ($bash) { return $bash.Source }

    throw "bash not found. Install Git for Windows (Git Bash) or add bash.exe to PATH."
}

$scriptDir = Split-Path -Parent $PSCommandPath
$bashPath = Get-BashPath
$target = Join-Path $scriptDir "prerequisite.sh"

if (-not (Test-Path $target)) {
    throw "Missing script: $target"
}

& $bashPath $target @args
