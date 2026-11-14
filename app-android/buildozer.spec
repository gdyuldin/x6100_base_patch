[app]
title = Xiegu X6100 Base Patch Firmware Uploader
package.name = x6100uploader
package.domain = org.x6100
source.dir = .
source.include_exts = py,png,jpg,kv,atlas,xgf
icon.filename = x6100-icon.png
# x6100_core.py + one recommended .xgf are copied in by build.sh before buildozer runs
version = 1.0
requirements = python3,kivy,paramiko,cryptography
orientation = portrait
fullscreen = 0
android.permissions = INTERNET,ACCESS_NETWORK_STATE,ACCESS_WIFI_STATE
android.api = 33
android.minapi = 24
android.ndk = 25b
android.accept_sdk_license = True
p4a.branch = v2024.01.21
[buildozer]
log_level = 2
