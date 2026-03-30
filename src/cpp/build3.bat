@echo off
cd /d C:\Users\dorgon.chang\.agents\skills\kano\kano-agent-backlog-skill\src\cpp
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cmake --preset windows-ninja-msvc
cmake --build --preset windows-ninja-msvc-debug
echo.
echo BUILD_EXITCODE=%ERRORLEVEL%
