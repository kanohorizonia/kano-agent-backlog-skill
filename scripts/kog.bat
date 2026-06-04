@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

call "%SCRIPT_DIR%kob.bat" %*
exit /b %ERRORLEVEL%
