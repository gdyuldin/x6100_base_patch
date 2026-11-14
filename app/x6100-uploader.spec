# PyInstaller spec - bundles the app + firmwares into one file.
# Usage (run on each OS): pyinstaller app/x6100-uploader.spec
# Output: dist/X6100-Uploader(.exe)
# Note: run from the REPO ROOT so ../firmwares resolves.
#   Windows separator for --add-data is ';', this spec handles all OSes.
import os

block_cipher = None
repo = os.path.abspath(os.path.join(SPECPATH, ".."))
fw_dir = os.path.join(repo, "firmwares")

a = Analysis(
    [os.path.join(repo, "app", "x6100_uploader.py")],
    pathex=[os.path.join(repo, "app")],
    binaries=[],
    datas=[(fw_dir, "firmwares")] if os.path.isdir(fw_dir) else [],
    hiddenimports=["paramiko", "cryptography", "bcrypt", "nacl"],
    excludes=["kivy"],
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)
exe = EXE(
    pyz, a.scripts, a.binaries, a.zipfiles, a.datas, [],
    name="X6100-Uploader",
    console=False,
    upx=False,
)
