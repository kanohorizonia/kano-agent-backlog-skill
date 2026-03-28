param(
  [Parameter(Mandatory = $true)]
  [string]$Action,

  [string]$Path = "",
  [string]$Root = "",
  [string]$BuildDir = "",
  [string]$Config = "Debug",
  [string]$Generator = "Ninja",
  [string]$Arch = "x64",
  [string]$CoverageBuildDir = "",
  [string]$Vcvars = "",
  [string]$ConfigurePreset = "",
  [string]$BuildPreset = "",
  [string]$VcvarsVersion = "14.44.35207"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppRoot = Resolve-Path (Join-Path $scriptDir "..\..\..")
$helperPath = Join-Path $cppRoot "shared\infra\scripts\windows\windows_preset_helper.ps1"
if (-not (Test-Path -LiteralPath $helperPath)) {
  throw "infra windows preset helper not found: $helperPath"
}

& $helperPath @PSBoundParameters
exit $LASTEXITCODE
