@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "SKILL_ROOT=%SCRIPT_DIR%\.."
for %%I in ("%SKILL_ROOT%") do set "SKILL_ROOT=%%~fI"
set "WORKSPACE_ROOT=%CD%"

:find_root
if exist "%WORKSPACE_ROOT%\.kano\backlog_config.toml" goto found_root
for %%I in ("%WORKSPACE_ROOT%") do set "PARENT_DIR=%%~dpI"
if /I "%PARENT_DIR:~0,-1%"=="%WORKSPACE_ROOT%" goto found_root
set "WORKSPACE_ROOT=%PARENT_DIR:~0,-1%"
goto find_root

:found_root
set "NATIVE_BIN=%SKILL_ROOT%\src\cpp\out\bin\windows-ninja-msvc\debug\kano-backlog.exe"

if not exist "%NATIVE_BIN%" (
  set "NATIVE_BIN=%SKILL_ROOT%\src\cpp\out\bin\windows-ninja-msvc\release\kano-backlog.exe"
)

if not exist "%NATIVE_BIN%" (
  echo Native CLI not found under: %SKILL_ROOT%\src\cpp\out\bin\windows-ninja-msvc\debug\kano-backlog.exe
  echo                           or: %SKILL_ROOT%\src\cpp\out\bin\windows-ninja-msvc\release\kano-backlog.exe
  echo Build it first with: bash .agents/skills/kano/kano-agent-backlog-skill/scripts/internal/self-build.sh
  exit /b 1
)

cd /d "%WORKSPACE_ROOT%"
"%NATIVE_BIN%" %*
