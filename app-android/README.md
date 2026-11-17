# X6100 Uploader - Android app (Kivy)

Same flow as the desktop GUI, phone UI. Core logic (`x6100_core.py`)
is shared, not duplicated - `build.sh` copies it in before packaging.

## Build the APK (Linux or CI)

```bash
cd app-android && ./build.sh   # → bin/*.apk
```

First build takes a while (downloads SDK/NDK). Or push tag `app-v*` /
run Actions → `build-apk.yml` artifact.
