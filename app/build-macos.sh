#!/usr/bin/env bash
# Build macOS .app. Run from repo root on macOS.
set -euo pipefail

# Positional args keep this working on bash 3.2 (/bin/bash on macOS), where an
# empty array expanded under `set -u` is an unbound-variable error.
set -- --windowed --name X6100-Uploader

ICON=assets/x6100-icon.png
if [ -f "$ICON" ]; then
  ICONSET=X6100-Uploader.iconset
  rm -rf "$ICONSET"
  mkdir -p "$ICONSET"
  for s in 16 32 128 256 512; do
    sips -z $s $s "$ICON" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) "$ICON" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o X6100-Uploader.icns
  rm -rf "$ICONSET"
  set -- "$@" --icon X6100-Uploader.icns
else
  echo "warning: $ICON missing, building without a custom icon"
fi

if [ -d firmwares ] && [ -n "$(ls -A firmwares)" ]; then
  set -- "$@" --add-data "firmwares:firmwares"
else
  echo "warning: no firmwares/ directory, the app will download firmware from releases"
fi

pip install pyinstaller paramiko
pyinstaller "$@" app/x6100_uploader.py
ls -d dist/X6100-Uploader.app
