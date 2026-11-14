@echo off
REM Build Windows .exe. Run from repo root on Windows.
pip install pyinstaller paramiko
pyinstaller app\x6100-uploader.spec
dir dist\X6100-Uploader.exe
