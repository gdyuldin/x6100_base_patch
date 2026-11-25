@echo off
setlocal
REM Build Windows .exe. Run from repo root on Windows.
pip install pyinstaller paramiko
if errorlevel 1 exit /b 1
pyinstaller app\x6100-uploader.spec
if errorlevel 1 exit /b 1
if not exist dist\X6100-Uploader.exe (
  echo ERROR: dist\X6100-Uploader.exe was not produced
  exit /b 1
)
dir dist\X6100-Uploader.exe
