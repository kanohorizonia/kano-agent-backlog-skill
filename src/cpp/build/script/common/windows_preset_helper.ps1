param(
  [Parameter(Mandatory = $true)]
  [string]$Action,

  [string]$Root = "",
  [string]$BuildDir = "",
  [string]$Config = "Debug",
  [string]$Generator = "Ninja",
  [string]$Arch = "x64",
  [string]$CoverageBuildDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Detect-VsDevCmd {
  $preferred = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
  )
  foreach ($candidate in $preferred) {
    if (Test-Path -LiteralPath $candidate) {
      Write-Output $candidate
      return
    }
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path -LiteralPath $vswhere) {
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "Common7\Tools\VsDevCmd.bat" 2>$null |
      Select-Object -First 1
    if ($found) {
      Write-Output $found
      return
    }
  }

  $roots = @()
  if ($env:ProgramFiles) {
    $roots += (Join-Path $env:ProgramFiles "Microsoft Visual Studio")
  }
  if (${env:ProgramFiles(x86)}) {
    $roots += (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio")
  }

  $scan = Get-ChildItem -Path $roots -Recurse -File -Filter VsDevCmd.bat -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
  if ($scan) {
    Write-Output $scan
  }
}

function Run-Build {
  if ([string]::IsNullOrWhiteSpace($Root) -or [string]::IsNullOrWhiteSpace($BuildDir)) {
    throw "Root and BuildDir are required"
  }

  $vsDevCmd = Detect-VsDevCmd
  if (-not $vsDevCmd) {
    throw "VsDevCmd.bat not found"
  }

  $rootPath = (Resolve-Path -LiteralPath $Root).Path
  $buildPath = Join-Path $rootPath $BuildDir
  New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

  $extraArgs = @()
  if ($env:KOB_COMPILER_LAUNCHER) {
    $launcher = $env:KOB_COMPILER_LAUNCHER
    $extraArgs += "-DCMAKE_C_COMPILER_LAUNCHER=$launcher"
    $extraArgs += "-DCMAKE_CXX_COMPILER_LAUNCHER=$launcher"
  }

  if ($env:CMAKE_OSX_ARCHITECTURES) {
    $extraArgs += "-DCMAKE_OSX_ARCHITECTURES=$env:CMAKE_OSX_ARCHITECTURES"
  }

  $joinedExtraArgs = ""
  if ($extraArgs.Count -gt 0) {
    $joinedExtraArgs = " " + ($extraArgs -join " ")
  }

  $configure = "call `"$vsDevCmd`" -arch=$Arch -host_arch=$Arch && cmake -S `"$rootPath`" -B `"$buildPath`" -G `"$Generator`" -DCMAKE_BUILD_TYPE=$Config$joinedExtraArgs && cmake --build `"$buildPath`""
  cmd.exe /d /c $configure
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

function Run-Coverage-Build {
  if ([string]::IsNullOrWhiteSpace($Root)) {
    throw "Root is required for coverage build"
  }
  $coverageDir = $CoverageBuildDir
  if ([string]::IsNullOrWhiteSpace($coverageDir)) {
    $coverageDir = "build/_intermediate/windows-ninja-msvc-coverage"
  }

  $vsDevCmd = Detect-VsDevCmd
  if (-not $vsDevCmd) {
    throw "VsDevCmd.bat not found"
  }

  $rootPath = (Resolve-Path -LiteralPath $Root).Path
  $buildPath = Join-Path $rootPath $coverageDir
  New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

  $extraArgs = @(
    "-DKANO_ENABLE_COVERAGE=ON"
  )
  if ($env:KOB_COMPILER_LAUNCHER) {
    $extraArgs += "-DCMAKE_C_COMPILER_LAUNCHER=$env:KOB_COMPILER_LAUNCHER"
    $extraArgs += "-DCMAKE_CXX_COMPILER_LAUNCHER=$env:KOB_COMPILER_LAUNCHER"
  }

  $joinedExtraArgs = " " + ($extraArgs -join " ")

  $configure = "call `"$vsDevCmd`" -arch=$Arch -host_arch=$Arch && cmake -S `"$rootPath`" -B `"$buildPath`" -G `"$Generator`" -DCMAKE_BUILD_TYPE=Debug$joinedExtraArgs && cmake --build `"$buildPath`""
  cmd.exe /d /c $configure
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

switch ($Action) {
  "detect-vsdevcmd" { Detect-VsDevCmd }
  "run-build" { Run-Build }
  "run-coverage-build" { Run-Coverage-Build }
  default { throw "Unknown action: $Action" }
}
