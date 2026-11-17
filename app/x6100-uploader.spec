# PyInstaller spec - bundles the app + firmwares into one file.
# Usage (run on each OS): pyinstaller app/x6100-uploader.spec
# Output: dist/X6100-Uploader(.exe)
# Note: run from the REPO ROOT so ../firmwares resolves.
#   Windows separator for --add-data is ';', this spec handles all OSes.
import os

block_cipher = None
repo = os.path.abspath(os.path.join(SPECPATH, ".."))
fw_dir = os.path.join(repo, "firmwares")

datas = []
if os.path.isdir(fw_dir):
    datas.append((fw_dir, "firmwares"))
icon64 = os.path.join(repo, "assets", "x6100-icon-64.png")
if os.path.isfile(icon64):
    datas.append((icon64, "assets"))

a = Analysis(
    [os.path.join(repo, "app", "x6100_uploader.py")],
    pathex=[os.path.join(repo, "app")],
    binaries=[],
    datas=datas,
    hiddenimports=["paramiko", "cryptography", "bcrypt", "nacl"],
    excludes=["kivy"],
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)
exe = EXE(
    pyz, a.scripts, a.binaries, a.zipfiles, a.datas, [],
    name="X6100-Uploader",
    icon=os.path.join(repo, "assets", "x6100-icon.ico"),
    console=False,
    upx=False,
)
