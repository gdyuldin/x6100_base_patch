#!/usr/bin/env bash
# Stage shared core + one bundled firmware, then build debug APK.
# Run on Linux (or CI):  cd app-android && ./build.sh
set -euo pipefail
cd "$(dirname "$0")"
export ANDROIDSDK="${ANDROIDSDK:-${ANDROID_HOME:-}}"
cp ../app/x6100_core.py ./x6100_core.py
if [ -f ../assets/x6100-icon.png ]; then
  cp ../assets/x6100-icon.png ./x6100-icon.png
else
  echo "warning: ../assets/x6100-icon.png missing, using a placeholder icon"
  pip install --quiet pillow
  python3 -c "from PIL import Image; Image.new('RGB',(512,512),(24,34,64)).save('x6100-icon.png')"
fi
mkdir -p firmwares
cp ../firmwares/X6100_BBFW_V1.1.6_230307001_r13.xgf firmwares/ 2>/dev/null || {
  latest="$(ls ../firmwares/*230307001*.xgf 2>/dev/null | sort -V | tail -1 || true)"
  [ -n "$latest" ] && cp "$latest" firmwares/
}
pip install buildozer
buildozer android debug
ls -lh bin/*.apk
