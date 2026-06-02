# Publish kano-agent-backlog-skill to PyPI (PowerShell version)
# 
# Usage:
#   .\scripts\publish_to_pypi.ps1 [-Environment test|prod]
#
# Parameters:
#   -Environment: "test" for test.pypi.org (default), "prod" for pypi.org
#
# Prerequisites:
#   - pip install twine
#   - Configure PyPI credentials in ~/.pypirc or use environment variables

param(
    [Parameter(Position=0)]
    [ValidateSet("test", "prod")]
    [string]$Environment = "test"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "PyPI Publishing Script" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Change to project root
Set-Location $ProjectRoot

# Check if twine is installed
try {
    $null = Get-Command twine -ErrorAction Stop
} catch {
    Write-Host "Error: twine is not installed" -ForegroundColor Red
    Write-Host "Install with: pip install twine"
    exit 1
}

# Get version from __version__.py
$Version = python -c "import sys; sys.path.insert(0, 'src/python'); from kano_backlog_core.__version__ import __version__; print(__version__)"
Write-Host "Package version: $Version" -ForegroundColor Green
Write-Host ""

# Check if dist/ directory exists and has files
if (-not (Test-Path "dist") -or (Get-ChildItem "dist" -ErrorAction SilentlyContinue).Count -eq 0) {
    Write-Host "Warning: dist/ directory is empty or doesn't exist" -ForegroundColor Yellow
    Write-Host "Building package first..."
    Write-Host ""
    
    # Clean previous builds
    Remove-Item -Path "dist", "build", "*.egg-info" -Recurse -Force -ErrorAction SilentlyContinue
    
    # Build package
    python -m build
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Error: Package build failed" -ForegroundColor Red
        exit 1
    }
    Write-Host ""
}

# List distribution files
Write-Host "Distribution files:"
Get-ChildItem "dist" | Format-Table Name, Length, LastWriteTime
Write-Host ""

# Verify distribution files exist
$WheelFile = Get-ChildItem "dist\*.whl" -ErrorAction SilentlyContinue | Select-Object -First 1
$SdistFile = Get-ChildItem "dist\*.tar.gz" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not $WheelFile -or -not $SdistFile) {
    Write-Host "Error: Missing distribution files" -ForegroundColor Red
    Write-Host "Expected: .whl and .tar.gz files in dist/"
    exit 1
}

# Check distribution with twine
Write-Host "Checking distribution files..."
twine check dist\*

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: Distribution check failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Confirm upload
if ($Environment -eq "prod") {
    Write-Host "==========================================" -ForegroundColor Yellow
    Write-Host "WARNING: You are about to upload to PRODUCTION PyPI" -ForegroundColor Yellow
    Write-Host "==========================================" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Package: kano-agent-backlog-skill"
    Write-Host "Version: $Version"
    Write-Host "Target: https://pypi.org"
    Write-Host ""
    
    $Confirm = Read-Host "Are you sure you want to continue? (yes/no)"
    
    if ($Confirm -ne "yes") {
        Write-Host "Upload cancelled."
        exit 0
    }
} else {
    Write-Host "Uploading to TEST PyPI" -ForegroundColor Green
    Write-Host "Package: kano-agent-backlog-skill"
    Write-Host "Version: $Version"
    Write-Host "Target: https://test.pypi.org"
    Write-Host ""
}

# Upload to PyPI
if ($Environment -eq "prod") {
    Write-Host "Uploading to PyPI..."
    twine upload dist\*
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "==========================================" -ForegroundColor Green
        Write-Host "Successfully uploaded to PyPI!" -ForegroundColor Green
        Write-Host "==========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Package URL: https://pypi.org/project/kano-agent-backlog-skill/$Version/"
        Write-Host ""
        Write-Host "Users can now install with:"
        Write-Host "  pip install kano-agent-backlog-skill"
        Write-Host ""
        Write-Host "Next steps:"
        Write-Host "  1. Create git tag: git tag -a v$Version -m 'Release $Version'"
        Write-Host "  2. Push tag: git push origin v$Version"
        Write-Host "  3. Create GitHub release with release notes"
        Write-Host "  4. Verify installation: pip install kano-agent-backlog-skill==$Version"
    } else {
        Write-Host "Error: Upload to PyPI failed" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "Uploading to Test PyPI..."
    twine upload --repository testpypi dist\*
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "==========================================" -ForegroundColor Green
        Write-Host "Successfully uploaded to Test PyPI!" -ForegroundColor Green
        Write-Host "==========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Package URL: https://test.pypi.org/project/kano-agent-backlog-skill/$Version/"
        Write-Host ""
        Write-Host "Test installation with:"
        Write-Host "  pip install --index-url https://test.pypi.org/simple/ kano-agent-backlog-skill"
        Write-Host ""
        Write-Host "If everything works, upload to production with:"
        Write-Host "  .\scripts\publish_to_pypi.ps1 -Environment prod"
    } else {
        Write-Host "Error: Upload to Test PyPI failed" -ForegroundColor Red
        exit 1
    }
}
