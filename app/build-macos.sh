#!/usr/bin/env bash
# Build macOS .app. Run from repo root on macOS.
set -euo pipefail
pip install pyinstaller paramiko
pyinstaller --windowed --name X6100-Uploader --add-data "firmwares:firmwares" app/x6100_uploader.py
ls -d dist/X6100-Uploader.app
