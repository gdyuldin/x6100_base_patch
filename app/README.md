# X6100 Uploader - desktop GUI (Windows / Linux / macOS)

Same job as `tools/install.sh` / `install.ps1`, with a window:
find the radio on WiFi, pick a `.xgf`, copy to `/usr/firmware`.
Flashing is still done on the radio (see popup after upload).

## Run from source

```bash
pip install -r requirements.txt
python x6100_uploader.py
```

## Build a double-clickable binary

- Linux (this machine): `bash app/build-linux.sh` → `X6100-Uploader-x86_64.AppImage`
- Windows (on Windows): `app\build-windows.bat` → `dist\X6100-Uploader.exe`
- macOS (on a Mac): `bash app/build-macos.sh` → `dist/X6100-Uploader.app`
- Or push tag `app-v*` / run Actions → artifacts from `.github/workflows/build-apps.yml`
