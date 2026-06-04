@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

call "%SCRIPT_DIR%kano-backlog.bat" %*
exit /b %ERRORLEVEL%
