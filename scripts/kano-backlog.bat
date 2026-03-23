@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SKILL_ROOT=%SCRIPT_DIR%\.."
for %%I in ("%SKILL_ROOT%") do set "SKILL_ROOT=%%~fI"
set "WORKSPACE_ROOT=%SKILL_ROOT%\..\..\..\.."
for %%I in ("%WORKSPACE_ROOT%") do set "WORKSPACE_ROOT=%%~fI"
set "NATIVE_BIN=%SKILL_ROOT%\src\cpp\build\windows-ninja-msvc\kano-backlog.exe"

if not exist "%NATIVE_BIN%" (
  echo Native CLI not found: %NATIVE_BIN%
  echo Build it first with: bash .agents/skills/kano/kano-agent-backlog-skill/scripts/internal/self-build.sh
  exit /b 1
)

cd /d "%WORKSPACE_ROOT%"
"%NATIVE_BIN%" %*
