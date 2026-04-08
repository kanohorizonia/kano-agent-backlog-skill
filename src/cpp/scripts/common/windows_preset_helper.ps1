param(
  [Parameter(Mandatory = $true)]
  [string]$Action,

  [string]$Path = "",
  [string]$Root = "",
  [string]$Preset = "",
  [string]$PreferredDrive = "",
  [string]$Mode = "auto",
  [string]$Drive = "",
  [string]$Vcvars = "",
  [string]$Arch = "x64",
  [string]$ConfigurePreset = "",
  [string]$BuildPreset = "",
  [string]$VcvarsVersion = "14.44.35207"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-SubstMappings() {
  $substMap = @{}
  cmd /d /c subst | ForEach-Object {
    if ($_ -match "^([A-Z]):\\:\s*=>\s*(.+)$") {
      $driveLetter = $matches[1].ToUpperInvariant()
      $targetPath = $matches[2].Trim()
      if (-not [string]::IsNullOrWhiteSpace($targetPath)) {
        $substMap[$driveLetter] = $targetPath
      }
    }
  }
  return $substMap
}

function Expand-SubstPath([string]$Value) {
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ""
  }

  $candidate = $Value.Trim()
  if ($candidate -notmatch "^(?<drive>[A-Za-z]):(?<rest>(\\|/).*)?$") {
    return $candidate
  }

  $driveLetter = $matches['drive'].ToUpperInvariant()
  $substMap = Get-SubstMappings
  if (-not $substMap.ContainsKey($driveLetter)) {
    return $candidate
  }

  $targetRoot = $substMap[$driveLetter] -replace "/", "\\"
  while ($targetRoot.Length -gt 3 -and $targetRoot.EndsWith("\\")) {
    $targetRoot = $targetRoot.Substring(0, $targetRoot.Length - 1)
  }

  $rest = $matches['rest']
  if ([string]::IsNullOrWhiteSpace($rest)) {
    return $targetRoot
  }

  $normalizedRest = $rest -replace "/", "\\"
  if ($normalizedRest.StartsWith("\\")) {
    return ($targetRoot + $normalizedRest)
  }
  return ($targetRoot + "\\" + $normalizedRest)
}

function Normalize-PathKey([string]$Value) {
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ""
  }

  $candidate = Expand-SubstPath $Value
  $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction SilentlyContinue
  if ($resolved) {
    $candidate = $resolved.Path
  }

  $candidate = $candidate -replace "/", "\\"
  while ($candidate.Length -gt 3 -and $candidate.EndsWith("\")) {
    $candidate = $candidate.Substring(0, $candidate.Length - 1)
  }
  return $candidate.ToLowerInvariant()
}

function Resolve-AbsoluteWindowsPath([string]$Value) {
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return ""
  }

  $candidate = Expand-SubstPath $Value
  $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction SilentlyContinue
  if ($resolved) {
    $candidate = $resolved.Path
  }

  $candidate = $candidate -replace "/", "\\"
  while ($candidate.Length -gt 3 -and $candidate.EndsWith("\")) {
    $candidate = $candidate.Substring(0, $candidate.Length - 1)
  }
  return $candidate
}

function Detect-Vcvarsall {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path -LiteralPath $vswhere) {
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Auxiliary\Build\vcvarsall.bat" 2>$null |
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
  $roots = $roots | Where-Object { Test-Path -LiteralPath $_ }
  if ($roots.Count -eq 0) {
    return
  }

  $scan = Get-ChildItem -Path $roots -Recurse -File -Filter vcvarsall.bat -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\VC\\Auxiliary\\Build\\vcvarsall\.bat$" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
  if ($scan) {
    Write-Output $scan
  }
}

function Resolve-SourceRoot([string]$InRoot, [string]$InPreset) {
  $decision = "keep"
  $effectiveRoot = $InRoot.Trim()
  if ([string]::IsNullOrWhiteSpace($effectiveRoot)) {
    $effectiveRoot = Resolve-AbsoluteWindowsPath $InRoot
  }
  if ([string]::IsNullOrWhiteSpace($effectiveRoot)) {
    $effectiveRoot = $InRoot
  }

  $buildDir = Join-Path $effectiveRoot ("out\obj\" + $InPreset)
  $cacheFile = Join-Path $buildDir "CMakeCache.txt"
  if (Test-Path -LiteralPath $cacheFile) {
    $cacheDirLine = Select-String -Path $cacheFile -Pattern "^CMAKE_CACHEFILE_DIR:INTERNAL=(.+)$" -ErrorAction SilentlyContinue |
      Select-Object -First 1
    $homeDirLine = Select-String -Path $cacheFile -Pattern "^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$" -ErrorAction SilentlyContinue |
      Select-Object -First 1

    $cachedDir = ""
    $cachedHome = ""
    if ($cacheDirLine -and $cacheDirLine.Matches.Count -gt 0) {
      $cachedDir = $cacheDirLine.Matches[0].Groups[1].Value.Trim()
    }
    if ($homeDirLine -and $homeDirLine.Matches.Count -gt 0) {
      $cachedHome = $homeDirLine.Matches[0].Groups[1].Value.Trim()
    }

    $buildKey = Normalize-PathKey $buildDir
    $cachedKey = Normalize-PathKey $cachedDir
    if (-not [string]::IsNullOrWhiteSpace($cachedKey) -and $cachedKey -ne $buildKey) {
      if (-not [string]::IsNullOrWhiteSpace($cachedHome) -and (Test-Path -LiteralPath $cachedHome)) {
        $decision = "use-cache-home"
        $effectiveRoot = $cachedHome
      } else {
        $decision = "clean-cache"
        Remove-Item -LiteralPath $buildDir -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
  }

  Write-Output ($decision + "|" + $effectiveRoot)
}

function Get-LongPathsEnabled() {
  try {
    $value = Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -ErrorAction Stop
    if ($null -ne $value -and $null -ne $value.LongPathsEnabled) {
      return ([int]$value.LongPathsEnabled -eq 1)
    }
  } catch {
  }
  return $false
}

function Get-PathRiskReport([string]$InRoot, [string]$InPreset, [int]$InPathLimit) {
  $root = Resolve-AbsoluteWindowsPath $InRoot
  if ([string]::IsNullOrWhiteSpace($root)) {
    $root = $InRoot
  }
  if ([string]::IsNullOrWhiteSpace($root)) {
    return @{
      Root = $InRoot
      CurrentLongestPath = $InRoot
      CurrentLongestLength = 0
      EstimatedLongestPath = ""
      EstimatedLongestLength = 0
      PathLimit = $InPathLimit
      ExceedsLimit = $false
    }
  }

  $currentLongestPath = $root
  $currentLongestLength = $root.Length
  Get-ChildItem -LiteralPath $root -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object {
    $full = $_.FullName
    if ($full.Length -gt $currentLongestLength) {
      $currentLongestLength = $full.Length
      $currentLongestPath = $full
    }
  }

  $estimateCandidates = @(
    ("out\obj\" + $InPreset + "\CMakeFiles\kano_git_core.dir\commands\private\commands\commit_push_cmd.cpp.obj"),
    ("out\obj\" + $InPreset + "\CMakeFiles\kano_git_core.dir\commands\private\commands\plan_cmd.cpp.obj"),
    ("out\obj\" + $InPreset + "\CMakeFiles\kano_git_core.dir\commands\private\commands\sync_cmd.cpp.obj"),
    ("out\obj\" + $InPreset + "\code\systems\kano_git_test_core\CMakeFiles\kano_git_test_core.dir\Release\generators\private\command_string_generator.cpp.obj.modmap"),
    ("out\obj\" + $InPreset + "\code\systems\kano_git_command\commit_plan\CMakeFiles\kano_git_cmd_commit_plan.dir\Release\private\commit_push_cmd.cpp.obj.modmap"),
    ("out\obj\" + $InPreset + "\code\tests\kano_git_tui_tests\CMakeFiles\kano_git_tui_tests.dir\Release\property\test_command_executor_properties.cpp.obj.modmap")
  )
  $estimatedLongestPath = $root
  $estimatedLongestLength = $root.Length
  foreach ($suffix in $estimateCandidates) {
    $candidate = Join-Path $root $suffix
    if ($candidate.Length -gt $estimatedLongestLength) {
      $estimatedLongestLength = $candidate.Length
      $estimatedLongestPath = $candidate
    }
  }

  $maxLen = [Math]::Max($currentLongestLength, $estimatedLongestLength)
  return @{
    Root = $root
    CurrentLongestPath = $currentLongestPath
    CurrentLongestLength = $currentLongestLength
    EstimatedLongestPath = $estimatedLongestPath
    EstimatedLongestLength = $estimatedLongestLength
    PathLimit = $InPathLimit
    ExceedsLimit = ($maxLen -ge $InPathLimit)
  }
}

function Prepare-SubstRoot([string]$InRoot, [string]$InPreset, [string]$InPreferredDrive, [string]$InMode) {
  $tab = [char]9
  $root = Resolve-AbsoluteWindowsPath $InRoot
  if ([string]::IsNullOrWhiteSpace($root)) {
    $root = $InRoot
  }

  $modeNorm = $InMode.Trim().ToLowerInvariant()
  if ([string]::IsNullOrWhiteSpace($modeNorm)) {
    $modeNorm = "auto"
  }
  $pathLimit = 240
  if ($env:KOG_WINDOWS_PATH_LIMIT -match "^\d+$") {
    $parsed = [int]$env:KOG_WINDOWS_PATH_LIMIT
    if ($parsed -gt 0) {
      $pathLimit = $parsed
    }
  }

  $longPathsEnabled = Get-LongPathsEnabled

  $risk = Get-PathRiskReport -InRoot $root -InPreset $InPreset -InPathLimit $pathLimit

  $shouldMap = $false
  if ($modeNorm -eq "on") {
    $shouldMap = $true
  } elseif ($modeNorm -eq "off") {
    $shouldMap = $false
  } else {
    $shouldMap = $risk.ExceedsLimit
  }
  if (-not [string]::IsNullOrWhiteSpace($InPreferredDrive)) {
    $shouldMap = $true
  }

  if (-not $shouldMap) {
    Write-Output ($root + $tab + $tab + "0")
    return
  }

  $substMap = @{}
  foreach ($entry in (Get-SubstMappings).GetEnumerator()) {
    $substMap[$entry.Key] = Resolve-AbsoluteWindowsPath $entry.Value
  }

  foreach ($k in @($substMap.Keys)) {
    if ($substMap[$k].ToLowerInvariant() -eq $root.ToLowerInvariant()) {
      Write-Output (($k + ":\") + $tab + ($k + ":") + $tab + "0")
      return
    }
  }

  $preferred = $null
  if ($InPreferredDrive -match "^[A-Za-z]$") {
    $preferred = $InPreferredDrive.ToUpperInvariant()
  } elseif ($InPreferredDrive -match "^([A-Za-z]):$") {
    $preferred = $Matches[1].ToUpperInvariant()
  }

  $candidates = New-Object System.Collections.Generic.List[string]
  if ($preferred) {
    [void]$candidates.Add($preferred)
  }
  foreach ($letter in @("Z","Y","X","W","V","U","T","S","R","Q","P","O","N","M","L","K","J","I","H","G","F","E","D")) {
    [void]$candidates.Add($letter)
  }

  $usedLetters = New-Object System.Collections.Generic.HashSet[string]
  Get-PSDrive -PSProvider FileSystem | ForEach-Object { [void]$usedLetters.Add($_.Name.ToUpperInvariant()) }
  foreach ($k in $substMap.Keys) {
    [void]$usedLetters.Add($k)
  }

  foreach ($driveLetter in $candidates) {
    if ($usedLetters.Contains($driveLetter)) {
      continue
    }
    $target = $driveLetter + ":"
    & cmd /d /c subst $target $root | Out-Null
    if (Test-Path -LiteralPath ($target + "\")) {
      Write-Output (($target + "\") + $tab + $target + $tab + "1")
      return
    }
  }

  if ($risk.ExceedsLimit) {
    Write-Error ("No available drive letter for SUBST while path risk exceeds limit.")
    Write-Error ("Root: " + $risk.Root)
    Write-Error ("LongPathsEnabled: " + $(if ($longPathsEnabled) { "1" } else { "0" }))
    Write-Error ("PathLimit: " + $risk.PathLimit)
    Write-Error ("CurrentLongestLength: " + $risk.CurrentLongestLength)
    Write-Error ("CurrentLongestPath: " + $risk.CurrentLongestPath)
    Write-Error ("EstimatedLongestLength: " + $risk.EstimatedLongestLength)
    Write-Error ("EstimatedLongestPath: " + $risk.EstimatedLongestPath)
    Write-Error ("Hint: enable Windows long paths (run as Administrator):")
    Write-Error ("  Set-ItemProperty -Path \"HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem\" -Name \"LongPathsEnabled\" -Value 1")
    Write-Error ("  Get-ItemProperty -Path \"HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem\" | Select-Object LongPathsEnabled")
    exit 3
  }

  Write-Output ($root + $tab + $tab + "0")
}

function Get-CMakeInputsFingerprint([string]$InRoot) {
  $root = Resolve-AbsoluteWindowsPath $InRoot
  if ([string]::IsNullOrWhiteSpace($root)) {
    $root = $InRoot
  }
  
  $inputFiles = @(
    (Join-Path $root "CMakeLists.txt"),
    (Join-Path $root "CMakePresets.json"),
    (Join-Path $root "vcpkg.json")
  )
  
  $subCMakeLists = Get-ChildItem -LiteralPath $root -Recurse -File -Filter "CMakeLists.txt" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
  if ($subCMakeLists) {
    $inputFiles += $subCMakeLists
  }
  
  $existingFiles = $inputFiles | Where-Object { Test-Path -LiteralPath $_ }
  
  if ($existingFiles.Count -eq 0) {
    return ""
  }
  
  $hash = [System.Security.Cryptography.SHA256]::Create()
  $combined = ""
  
  foreach ($file in ($existingFiles | Sort-Object)) {
    $content = Get-Content -LiteralPath $file -Raw -ErrorAction SilentlyContinue
    if ($content) {
      $bytes = [System.Text.Encoding]::UTF8.GetBytes($content)
      $hash.TransformBlock($bytes, 0, $bytes.Length, $null, 0) | Out-Null
    }
  }

  $launcherFingerprintParts = @(
    "KOG_COMPILER_LAUNCHER_RESOLVED=$($env:KOG_COMPILER_LAUNCHER_RESOLVED)",
    "SCCACHE_DIR=$($env:SCCACHE_DIR)",
    "CCACHE_DIR=$($env:CCACHE_DIR)",
    "KOG_CMAKE_CACHE_ARGS_JSON=$($env:KOG_CMAKE_CACHE_ARGS_JSON)"
  )
  foreach ($part in $launcherFingerprintParts) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($part)
    $hash.TransformBlock($bytes, 0, $bytes.Length, $null, 0) | Out-Null
  }
  
  $hash.TransformFinalBlock(@(), 0, 0) | Out-Null
  $fingerprint = [System.BitConverter]::ToString($hash.Hash) -replace "-", ""
  $hash.Dispose()
  
  return $fingerprint
}

function Get-AdditionalCMakeCacheArguments() {
  if ([string]::IsNullOrWhiteSpace($env:KOG_CMAKE_CACHE_ARGS_JSON)) {
    return @()
  }

  try {
    $parsed = $env:KOG_CMAKE_CACHE_ARGS_JSON | ConvertFrom-Json -ErrorAction Stop
  } catch {
    Write-Error ("Invalid KOG_CMAKE_CACHE_ARGS_JSON: {0}" -f $_.Exception.Message)
    exit 4
  }

  $arguments = New-Object System.Collections.Generic.List[string]
  foreach ($property in $parsed.PSObject.Properties) {
    $name = [string]$property.Name
    if ([string]::IsNullOrWhiteSpace($name)) {
      continue
    }

    $value = ""
    if ($null -ne $property.Value) {
      $value = [string]$property.Value
    }
    [void]$arguments.Add((Format-CMakeCacheArgument -Name $name -Value $value))
  }

  return $arguments.ToArray()
}

function Get-CMakeCacheValue([string]$InCacheFile, [string]$InKey) {
  if (-not (Test-Path -LiteralPath $InCacheFile)) {
    return ""
  }

  $match = Select-String -Path $InCacheFile -Pattern ("^" + [Regex]::Escape($InKey) + ":[^=]*=(.*)$") -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($match -and $match.Matches.Count -gt 0) {
    return $match.Matches[0].Groups[1].Value.Trim()
  }
  return ""
}

function Test-CompilerLauncherValueMatches([string]$Expected, [string]$Actual) {
  $expectedValue = ""
  $actualValue = ""
  if ($null -ne $Expected) {
    $expectedValue = $Expected.Trim()
  }
  if ($null -ne $Actual) {
    $actualValue = $Actual.Trim()
  }

  if ([string]::IsNullOrWhiteSpace($expectedValue) -and [string]::IsNullOrWhiteSpace($actualValue)) {
    return $true
  }
  if ([string]::IsNullOrWhiteSpace($expectedValue) -or [string]::IsNullOrWhiteSpace($actualValue)) {
    return $false
  }

  $expectedPath = Resolve-AbsoluteWindowsPath $expectedValue
  $actualPath = Resolve-AbsoluteWindowsPath $actualValue
  if ((-not [string]::IsNullOrWhiteSpace($expectedPath)) -and (-not [string]::IsNullOrWhiteSpace($actualPath))) {
    if ((Normalize-PathKey $expectedPath) -eq (Normalize-PathKey $actualPath)) {
      return $true
    }
  }

  if ($expectedValue.ToLowerInvariant() -eq $actualValue.ToLowerInvariant()) {
    return $true
  }

  $expectedLeaf = [System.IO.Path]::GetFileNameWithoutExtension($expectedValue)
  $actualLeaf = [System.IO.Path]::GetFileNameWithoutExtension($actualValue)
  if ((-not [string]::IsNullOrWhiteSpace($expectedLeaf)) -and ($expectedLeaf.ToLowerInvariant() -eq $actualLeaf.ToLowerInvariant())) {
    return $true
  }

  return $false
}

function Test-CompilerLauncherMatchesCache([string]$InBuildDir) {
  $cacheFile = Join-Path $InBuildDir "CMakeCache.txt"
  if (-not (Test-Path -LiteralPath $cacheFile)) {
    return $false
  }

  $expectedLauncher = $env:KOG_COMPILER_LAUNCHER_RESOLVED
  $cachedCLauncher = Get-CMakeCacheValue -InCacheFile $cacheFile -InKey "CMAKE_C_COMPILER_LAUNCHER"
  $cachedCxxLauncher = Get-CMakeCacheValue -InCacheFile $cacheFile -InKey "CMAKE_CXX_COMPILER_LAUNCHER"

  return ((Test-CompilerLauncherValueMatches -Expected $expectedLauncher -Actual $cachedCLauncher) -and
          (Test-CompilerLauncherValueMatches -Expected $expectedLauncher -Actual $cachedCxxLauncher))
}

function Get-StoredConfigureFingerprint([string]$InBuildDir) {
  $fingerprintFile = Join-Path $InBuildDir "cmake-inputs.fingerprint"
  if (Test-Path -LiteralPath $fingerprintFile) {
    return (Get-Content -LiteralPath $fingerprintFile -Raw -ErrorAction SilentlyContinue).Trim()
  }
  return $null
}

function Set-StoredConfigureFingerprint([string]$InBuildDir, [string]$InFingerprint) {
  $fingerprintFile = Join-Path $InBuildDir "cmake-inputs.fingerprint"
  Set-Content -LiteralPath $fingerprintFile -Value $InFingerprint -NoNewline -ErrorAction SilentlyContinue
}

function Get-CompilerLauncherCacheKind() {
  $resolved = $env:KOG_COMPILER_LAUNCHER_RESOLVED
  if ([string]::IsNullOrWhiteSpace($resolved)) {
    return ""
  }
  $leaf = [System.IO.Path]::GetFileNameWithoutExtension($resolved).ToLowerInvariant()
  if ($leaf -eq "sccache" -or $leaf -eq "ccache") {
    return $leaf
  }
  return ""
}

function Resolve-CommandExecutablePath([string]$CommandValue) {
  if ([string]::IsNullOrWhiteSpace($CommandValue)) {
    return ""
  }

  $candidate = $CommandValue.Trim()
  $literalPath = Resolve-AbsoluteWindowsPath $candidate
  if ((-not [string]::IsNullOrWhiteSpace($literalPath)) -and (Test-Path -LiteralPath $literalPath)) {
    return $literalPath
  }

  try {
    $command = Get-Command $candidate -ErrorAction Stop | Select-Object -First 1
    if ($command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
      return (Resolve-AbsoluteWindowsPath $command.Source)
    }
    if ($command -and -not [string]::IsNullOrWhiteSpace($command.Path)) {
      return (Resolve-AbsoluteWindowsPath $command.Path)
    }
  } catch {
  }

  return ""
}

function Get-EffectiveCompilerLauncherPath() {
  $resolved = $env:KOG_COMPILER_LAUNCHER_RESOLVED
  if ([string]::IsNullOrWhiteSpace($resolved)) {
    return ""
  }
  return (Resolve-CommandExecutablePath $resolved)
}

function Normalize-CompilerCacheEnvironment() {
  $kind = Get-CompilerLauncherCacheKind
  if ($kind -eq "sccache") {
    if (-not [string]::IsNullOrWhiteSpace($env:SCCACHE_DIR)) {
      $env:SCCACHE_DIR = Resolve-AbsoluteWindowsPath $env:SCCACHE_DIR
    }
    return
  }
  if ($kind -eq "ccache") {
    if (-not [string]::IsNullOrWhiteSpace($env:CCACHE_DIR)) {
      $env:CCACHE_DIR = Resolve-AbsoluteWindowsPath $env:CCACHE_DIR
    }
  }
}

function Get-SccacheReportedCacheLocation() {
  try {
    $output = & sccache --show-stats 2>$null | Out-String
    $match = [Regex]::Match($output, 'Cache location\s+Local disk: "([^"]+)"')
    if ($match.Success) {
      return (Resolve-AbsoluteWindowsPath $match.Groups[1].Value)
    }
  } catch {
  }
  return ""
}

function Ensure-CompilerCacheServerConfiguration() {
  $kind = Get-CompilerLauncherCacheKind
  if ($kind -ne "sccache") {
    return
  }

  $desired = ""
  if (-not [string]::IsNullOrWhiteSpace($env:SCCACHE_DIR)) {
    $desired = Resolve-AbsoluteWindowsPath $env:SCCACHE_DIR
    $env:SCCACHE_DIR = $desired
  }
  if ([string]::IsNullOrWhiteSpace($desired)) {
    return
  }

  $reported = Get-SccacheReportedCacheLocation
  if ((-not [string]::IsNullOrWhiteSpace($reported)) -and ((Normalize-PathKey $reported) -ne (Normalize-PathKey $desired))) {
    Write-Host ("[launcher][compiler-cache][info] restarting sccache server for dir={0}" -f $desired)
    try {
      & sccache --stop-server | Out-Null
    } catch {
      Write-Host ("[launcher][compiler-cache][warn] unable to stop sccache server: {0}" -f $_.Exception.Message)
    }
  }
}

function Format-CMakeCacheArgument([string]$Name, [string]$Value) {
  $escapedValue = $Value.Replace('"', '""')
  return ('"-D{0}={1}"' -f $Name, $escapedValue)
}

function Write-CompilerCacheSummary([string]$Phase, [string]$BuildDir = "") {
  $kind = Get-CompilerLauncherCacheKind
  if ([string]::IsNullOrWhiteSpace($kind)) {
    if ($env:KOG_FASTBUILD_ENABLED -eq "1") {
      $dir = $env:FASTBUILD_CACHE_PATH
      if ([string]::IsNullOrWhiteSpace($dir) -and -not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $dir = Join-Path $env:USERPROFILE ".kano\cache\fastbuild"
      }
      Write-Host ("[launcher][compiler-cache][{0}] kind=fastbuild dir={1}" -f $Phase, $dir)
      if ((-not [string]::IsNullOrWhiteSpace($env:KOG_FASTBUILD_EXECUTABLE)) -and (-not [string]::IsNullOrWhiteSpace($BuildDir))) {
        $bffPath = Join-Path $BuildDir "fbuild.bff"
        try {
          if (Test-Path -LiteralPath $bffPath) {
            & $env:KOG_FASTBUILD_EXECUTABLE -config $bffPath -cacheinfo
          }
        } catch {
          Write-Host ("[launcher][compiler-cache][{0}] unable to read FASTBuild cache info: {1}" -f $Phase, $_.Exception.Message)
        }
      }
    }
    return
  }

  if ($kind -eq "sccache") {
    $dir = $env:SCCACHE_DIR
    if ([string]::IsNullOrWhiteSpace($dir)) {
      $dir = Join-Path $env:USERPROFILE ".kano\cache\sccache"
    }
    Write-Host ("[launcher][compiler-cache][{0}] kind=sccache dir={1}" -f $Phase, $dir)
    try {
      & sccache --show-stats
    } catch {
      Write-Host ("[launcher][compiler-cache][{0}] unable to read sccache stats: {1}" -f $Phase, $_.Exception.Message)
    }
    return
  }

  if ($kind -eq "ccache") {
    $dir = $env:CCACHE_DIR
    if ([string]::IsNullOrWhiteSpace($dir)) {
      $dir = Join-Path $env:USERPROFILE ".kano\cache\ccache"
    }
    Write-Host ("[launcher][compiler-cache][{0}] kind=ccache dir={1}" -f $Phase, $dir)
    try {
      & ccache --show-stats
    } catch {
      Write-Host ("[launcher][compiler-cache][{0}] unable to read ccache stats: {1}" -f $Phase, $_.Exception.Message)
    }
  }
}

function Run-Preset([string]$InRoot,
                    [string]$InVcvars,
                    [string]$InArch,
                    [string]$InConfigurePreset,
                    [string]$InBuildPreset,
                    [string]$InVcvarsVersion) {
  Set-Location -LiteralPath $InRoot
  Normalize-CompilerCacheEnvironment
  Ensure-CompilerCacheServerConfiguration
  
  $buildDir = Join-Path $InRoot "out\obj\$InConfigurePreset"
  
  $currentFingerprint = Get-CMakeInputsFingerprint -InRoot $InRoot
  $storedFingerprint = Get-StoredConfigureFingerprint -InBuildDir $buildDir
  
  $shouldConfigure = $true
  if ($currentFingerprint -and $storedFingerprint -and ($currentFingerprint -eq $storedFingerprint)) {
    $shouldConfigure = $false
    Write-Host "[launcher][cmake][skip] Configure inputs unchanged, skipping cmake --preset"
  } else {
    Write-Host "[launcher][cmake][info] Configure inputs changed or no cached fingerprint, running cmake --preset"
  }

  if (-not $shouldConfigure) {
    if (-not (Test-CompilerLauncherMatchesCache -InBuildDir $buildDir)) {
      $shouldConfigure = $true
      Write-Host "[launcher][cmake][info] Compiler launcher cache mismatch detected, rerunning cmake --preset"
    }
  }
  
  $quotedVcvars = '"' + $InVcvars + '"'
  
  if ($shouldConfigure) {
    $configureCommand = "cmake --preset $InConfigurePreset"
    foreach ($additionalArgument in (Get-AdditionalCMakeCacheArguments)) {
      $configureCommand += " " + $additionalArgument
    }
    $launcherPath = Get-EffectiveCompilerLauncherPath
    $isFastBuild = ($env:KOG_FASTBUILD_ENABLED -eq "1")
    if ($isFastBuild) {
      if (-not [string]::IsNullOrWhiteSpace($env:FASTBUILD_CACHE_PATH)) {
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_FASTBUILD_CACHE_PATH" -Value (Resolve-AbsoluteWindowsPath $env:FASTBUILD_CACHE_PATH))
      }
      if (-not [string]::IsNullOrWhiteSpace($env:KOG_FASTBUILD_EXECUTABLE)) {
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_MAKE_PROGRAM" -Value (Resolve-AbsoluteWindowsPath $env:KOG_FASTBUILD_EXECUTABLE))
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_FASTBUILD_EXECUTABLE" -Value (Resolve-AbsoluteWindowsPath $env:KOG_FASTBUILD_EXECUTABLE))
      }
    } elseif (-not [string]::IsNullOrWhiteSpace($launcherPath)) {
      $env:KOG_COMPILER_LAUNCHER = $launcherPath
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "KOG_COMPILER_LAUNCHER" -Value $launcherPath)
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_C_COMPILER_LAUNCHER" -Value $launcherPath)
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_CXX_COMPILER_LAUNCHER" -Value $launcherPath)
    }
    $cmd = "call $quotedVcvars $InArch -vcvars_ver=$InVcvarsVersion && $configureCommand"
    cmd.exe /d /s /c $cmd
    $configureExitCode = $LASTEXITCODE
    if ($configureExitCode -ne 0) {
      Write-Error "cmake --preset failed with exit code $configureExitCode"
      exit $configureExitCode
    }
    if ($currentFingerprint) {
      Set-StoredConfigureFingerprint -InBuildDir $buildDir -InFingerprint $currentFingerprint
    }
  }
  
  Write-CompilerCacheSummary "before-build" $buildDir
  $buildCommand = "cmake --build --preset $InBuildPreset"
  if ($env:KOG_FASTBUILD_ENABLED -eq "1") {
    $buildCommand += " -- -cache"
    if ($env:KOG_FASTBUILD_DISTRIBUTED -eq "1") {
      $buildCommand += " -dist"
    }
  }
  $cmd = "call $quotedVcvars $InArch -vcvars_ver=$InVcvarsVersion && $buildCommand"
  cmd.exe /d /s /c $cmd
  Write-CompilerCacheSummary "after-build" $buildDir
  exit $LASTEXITCODE
}

function Configure-Preset([string]$InRoot,
                          [string]$InVcvars,
                          [string]$InArch,
                          [string]$InConfigurePreset,
                          [string]$InVcvarsVersion) {
  Set-Location -LiteralPath $InRoot
  Normalize-CompilerCacheEnvironment
  Ensure-CompilerCacheServerConfiguration

  $buildDir = Join-Path $InRoot "out\obj\$InConfigurePreset"
  $currentFingerprint = Get-CMakeInputsFingerprint -InRoot $InRoot
  $storedFingerprint = Get-StoredConfigureFingerprint -InBuildDir $buildDir

  $shouldConfigure = $true
  if ($currentFingerprint -and $storedFingerprint -and ($currentFingerprint -eq $storedFingerprint)) {
    $shouldConfigure = $false
    Write-Host "[launcher][cmake][skip] Configure inputs unchanged, skipping cmake --preset"
  }

  if (-not $shouldConfigure) {
    if (-not (Test-CompilerLauncherMatchesCache -InBuildDir $buildDir)) {
      $shouldConfigure = $true
      Write-Host "[launcher][cmake][info] Compiler launcher cache mismatch detected, rerunning cmake --preset"
    }
  }

  $quotedVcvars = '"' + $InVcvars + '"'

  if ($shouldConfigure) {
    $configureCommand = "cmake --preset $InConfigurePreset"
    foreach ($additionalArgument in (Get-AdditionalCMakeCacheArguments)) {
      $configureCommand += " " + $additionalArgument
    }
    $launcherPath = Get-EffectiveCompilerLauncherPath
    $isFastBuild = ($env:KOG_FASTBUILD_ENABLED -eq "1")
    if ($isFastBuild) {
      if (-not [string]::IsNullOrWhiteSpace($env:FASTBUILD_CACHE_PATH)) {
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_FASTBUILD_CACHE_PATH" -Value (Resolve-AbsoluteWindowsPath $env:FASTBUILD_CACHE_PATH))
      }
      if (-not [string]::IsNullOrWhiteSpace($env:KOG_FASTBUILD_EXECUTABLE)) {
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_MAKE_PROGRAM" -Value (Resolve-AbsoluteWindowsPath $env:KOG_FASTBUILD_EXECUTABLE))
        $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_FASTBUILD_EXECUTABLE" -Value (Resolve-AbsoluteWindowsPath $env:KOG_FASTBUILD_EXECUTABLE))
      }
    } elseif (-not [string]::IsNullOrWhiteSpace($launcherPath)) {
      $env:KOG_COMPILER_LAUNCHER = $launcherPath
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "KOG_COMPILER_LAUNCHER" -Value $launcherPath)
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_C_COMPILER_LAUNCHER" -Value $launcherPath)
      $configureCommand += " " + (Format-CMakeCacheArgument -Name "CMAKE_CXX_COMPILER_LAUNCHER" -Value $launcherPath)
    }
    $cmd = "call $quotedVcvars $InArch -vcvars_ver=$InVcvarsVersion && $configureCommand"
    cmd.exe /d /s /c $cmd
    $configureExitCode = $LASTEXITCODE
    if ($configureExitCode -ne 0) {
      Write-Error "cmake --preset failed with exit code $configureExitCode"
      exit $configureExitCode
    }
    if ($currentFingerprint) {
      Set-StoredConfigureFingerprint -InBuildDir $buildDir -InFingerprint $currentFingerprint
    }
  }

  $solution = Get-ChildItem -LiteralPath $buildDir -Filter *.sln -Recurse -ErrorAction SilentlyContinue |
    Sort-Object FullName |
    Select-Object -First 1 -ExpandProperty FullName
  if ($solution) {
    Write-Output $solution
  }
  exit 0
}

switch ($Action.ToLowerInvariant()) {
  "test-path" {
    if (Test-Path -LiteralPath $Path) { exit 0 } else { exit 1 }
  }
  "detect-vcvarsall" {
    Detect-Vcvarsall
    exit 0
  }
  "resolve-source-root" {
    Resolve-SourceRoot $Root $Preset
    exit 0
  }
  "prepare-subst-root" {
    Prepare-SubstRoot $Root $Preset $PreferredDrive $Mode
    exit 0
  }
  "cleanup-subst" {
    if (-not [string]::IsNullOrWhiteSpace($Drive)) {
      $targetDrive = $Drive.Trim()
      if ($targetDrive -match "^[A-Za-z]$") {
        $targetDrive = $targetDrive + ":"
      }
      & cmd /d /c subst $targetDrive /d | Out-Null
    }
    exit 0
  }
  "run-preset" {
    Run-Preset $Root $Vcvars $Arch $ConfigurePreset $BuildPreset $VcvarsVersion
  }
  "configure-preset" {
    Configure-Preset $Root $Vcvars $Arch $ConfigurePreset $VcvarsVersion
  }
  default {
    Write-Error "Unknown action: $Action"
    exit 2
  }
}
