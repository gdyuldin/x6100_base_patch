#!/usr/bin/env bash
# Build macOS .app. Run from repo root on macOS.
set -euo pipefail
ICONSET=X6100-Uploader.iconset
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s assets/x6100-icon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) assets/x6100-icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o X6100-Uploader.icns
rm -rf "$ICONSET"
pip install pyinstaller paramiko
pyinstaller --windowed --name X6100-Uploader --icon X6100-Uploader.icns --add-data "firmwares:firmwares" app/x6100_uploader.py
ls -d dist/X6100-Uploader.app
