#!/usr/bin/env bash
# Build Linux single-binary + AppImage. Run from repo root.
set -euo pipefail
pip install pyinstaller paramiko
pyinstaller app/x6100-uploader.spec
BIN=dist/X6100-Uploader
file "$BIN"
if ! command -v appimagetool >/dev/null 2>&1; then
  echo "Fetching appimagetool (CI runners don't have it)..."
  curl -fsSL -o /tmp/appimagetool.AppImage https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x /tmp/appimagetool.AppImage
  (cd /tmp && /tmp/appimagetool.AppImage --appimage-extract >/dev/null)
  APPIMAGETOOL=/tmp/squashfs-root/AppRun
else
  APPIMAGETOOL=appimagetool
fi
rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/share/applications AppDir/usr/share/icons/hicolor/256x256/apps
cp "$BIN" AppDir/usr/bin/
cp app/X6100-Uploader.desktop AppDir/usr/share/applications/ 2>/dev/null || {
  printf '[Desktop Entry]\nType=Application\nName=Xiegu X6100 Base Patch Firmware Uploader\nExec=X6100-Uploader\nIcon=x6100-uploader\nCategories=HamRadio;Network;\n' \
    > AppDir/usr/share/applications/X6100-Uploader.desktop
}
# icon optional: drop a 256x256 png at app/icon.png and it gets picked up
[ -f assets/x6100-icon-256.png ] && cp assets/x6100-icon-256.png AppDir/usr/share/icons/hicolor/256x256/apps/x6100-uploader.png || \
python3 -c "from PIL import Image; Image.new('RGB',(256,256),(20,30,60)).save('AppDir/usr/share/icons/hicolor/256x256/apps/x6100-uploader.png')"
cat > AppDir/AppRun <<'EOF'
#!/bin/sh
exec "$APPDIR/usr/bin/X6100-Uploader" "$@"
EOF
chmod +x AppDir/AppRun
cp AppDir/usr/share/applications/X6100-Uploader.desktop AppDir/
cp AppDir/usr/share/icons/hicolor/256x256/apps/x6100-uploader.png AppDir/ 2>/dev/null || true
ARCH=x86_64 "$APPIMAGETOOL" AppDir X6100-Uploader-x86_64.AppImage
ls -lh X6100-Uploader-x86_64.AppImage
