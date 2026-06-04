@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "BASH_EXE="

if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH_EXE=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH_EXE if exist "%ProgramFiles%\Git\usr\bin\bash.exe" set "BASH_EXE=%ProgramFiles%\Git\usr\bin\bash.exe"
if not defined BASH_EXE if exist "%ProgramW6432%\Git\bin\bash.exe" set "BASH_EXE=%ProgramW6432%\Git\bin\bash.exe"
if not defined BASH_EXE if exist "%ProgramW6432%\Git\usr\bin\bash.exe" set "BASH_EXE=%ProgramW6432%\Git\usr\bin\bash.exe"

if not defined BASH_EXE (
  for /f "delims=" %%I in ('where bash.exe 2^>nul') do (
    set "BASH_EXE=%%~fI"
    goto run_wrapper
  )
)

:run_wrapper
if not defined BASH_EXE (
  echo Error: bash.exe not found. Install Git for Windows or add bash.exe to PATH. 1>&2
  exit /b 1
)

"%BASH_EXE%" "%SCRIPT_DIR%kano-backlog" %*
exit /b %ERRORLEVEL%
