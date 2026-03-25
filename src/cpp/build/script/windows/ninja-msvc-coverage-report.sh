#!/usr/bin/env bash
# =============================================================================
# Windows MSVC Coverage - Generate Report
# =============================================================================
# Converts .coverage binary to XML/Cobertura for CI ingestion.
# Note: Microsoft.CodeCoverage.Console does NOT produce HTML directly.
# The .coverage binary can be opened in Visual Studio for interactive reports,
# or converted to Cobertura XML for CI tools (Codecov, etc.)
#
# Usage:
#   bash ninja-msvc-coverage-report.sh
#
# Output:
#   $KOB_COVERAGE_ROOT/coverage/coverage.xml
#   $KOB_COVERAGE_ROOT/coverage/coverage.cobertura.xml
#   $KOB_COVERAGE_ROOT/coverage/summary.txt (text summary)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KOB_CPP_ROOT="${KOB_CPP_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
KOB_COVERAGE_ROOT="${KOB_COVERAGE_ROOT:-${KOB_CPP_ROOT}/out/coverage}"

# Detect CodeCoverage.Console
KOB_CODE_COVERAGE_CONSOLE="${KOB_CODE_COVERAGE_CONSOLE:-}"
if [[ -z "$KOB_CODE_COVERAGE_CONSOLE" ]]; then
  for path in \
    "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe" \
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe"
  do
    if [[ -x "$path" ]]; then
      KOB_CODE_COVERAGE_CONSOLE="$path"
      break
    fi
  done
fi

if [[ -z "$KOB_CODE_COVERAGE_CONSOLE" || ! -x "$KOB_CODE_COVERAGE_CONSOLE" ]]; then
  echo "[ERROR] Microsoft.CodeCoverage.Console not found." >&2
  echo "[ERROR] Set KOB_CODE_COVERAGE_CONSOLE to the path." >&2
  exit 1
fi

# Collect all .coverage files
COVERAGE_FILES=()
for f in "$KOB_COVERAGE_ROOT"/*.coverage; do
  if [[ -f "$f" ]]; then
    COVERAGE_FILES+=("-i" "$f")
  fi
done

if [[ ${#COVERAGE_FILES[@]} -eq 0 ]]; then
  echo "[ERROR] No .coverage files found in: $KOB_COVERAGE_ROOT" >&2
  echo "[ERROR] Run ninja-msvc-coverage-run.sh first." >&2
  exit 1
fi

mkdir -p "$KOB_COVERAGE_ROOT"

# Convert to XML (native format) - merge all files
echo "[coverage-report] Merging and converting to XML..."
MERGED_XML="$KOB_COVERAGE_ROOT/coverage.xml"
"$KOB_CODE_COVERAGE_CONSOLE" merge "${COVERAGE_FILES[@]}" \
  -o "$MERGED_XML" \
  -f xml \
  2>&1 | grep -v "^Logo" || true

# Convert to Cobertura XML (for Codecov/CI tools)
echo "[coverage-report] Converting to Cobertura XML..."
MERGED_COBERTURA="$KOB_COVERAGE_ROOT/coverage.cobertura.xml"
"$KOB_CODE_COVERAGE_CONSOLE" merge "${COVERAGE_FILES[@]}" \
  -o "$MERGED_COBERTURA" \
  -f cobertura \
  2>&1 | grep -v "^Logo" || true

echo "[coverage-report] Reports:"
echo "  XML:        $MERGED_XML"
echo "  Cobertura:  $MERGED_COBERTURA"

# Generate text summary via PowerShell
echo "[coverage-report] Generating text summary via PowerShell..."
powershell -NoProfile -ExecutionPolicy Bypass -Command "
\$ErrorActionPreference = 'Stop'
try {
    Add-Type -Path \"${KOB_CODE_COVERAGE_CONSOLE%/*}/*.dll\" -ErrorAction SilentlyContinue
    \$coverageFiles = @(
        Get-ChildItem '$KOB_COVERAGE_ROOT' -Filter '*.coverage' | ForEach-Object { \$_.FullName }
    )
    if (\$coverageFiles.Count -eq 0) { exit 0 }
    \$merged = New-Object System.Collections.Generic.List[Microsoft.VisualStudio.Coverage.Analysis.CoverageInfo]
    foreach (\$f in \$coverageFiles) {
        try {
            \$ci = [Microsoft.VisualStudio.Coverage.Analysis.CoverageInfo]::CreateFromFile(\$f)
            \$merged.Add(\$ci)
        } catch {}
    }
    if (\$merged.Count -eq 0) { Write-Output 'No coverage data could be loaded.'; exit 0 }
    \$session = \$merged[0].BuildSession()
    \$stats = \$session.ExecutedCodeCoverageStatistics
    Write-Output '=== Coverage Summary ==='
    Write-Output \"Lines covered:     \$(\$stats.LinesCovered)\"
    Write-Output \"Lines not covered: \$(\$stats.LinesNotCovered)\"
    \$pct = [math]::Round(\$stats.LineCoverage * 100, 2)
    Write-Output \"Line coverage:    \$pct%\"
    Write-Output \"Blocks covered:   \$(\$stats.BlocksCovered)\"
    Write-Output \"Blocks not covered: \$(\$stats.BlocksNotCovered)\"
    \$bpct = [math]::Round(\$stats.BlockCoverage * 100, 2)
    Write-Output \"Block coverage:   \$bpct%\"
} catch {
    Write-Output 'Could not generate summary (requires VS SDK).'
    Write-Output 'Open .coverage files in Visual Studio for interactive report.'
}
" > "$KOB_COVERAGE_ROOT/summary.txt" 2>&1 || true

cat "$KOB_COVERAGE_ROOT/summary.txt"
echo ""
echo "[coverage-report] Done. Summary: $KOB_COVERAGE_ROOT/summary.txt"
