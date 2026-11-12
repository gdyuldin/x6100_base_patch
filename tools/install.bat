@echo off
REM X6100 BASE firmware installer launcher (Windows).
REM Double-click this file. It runs tools\install.ps1 for you.
set PS1=%~dp0install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
echo.
pause
