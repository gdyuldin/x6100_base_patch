"""X6100 BASE firmware uploader - shared core (no GUI).

Used by the desktop (tkinter) app and the Android (Kivy) app.
All network/SSH/SFTP logic lives here so both UIs stay thin.
"""
import concurrent.futures
import json
import os
import re
import socket
import subprocess
import sys
import urllib.request

REPO = "gdyuldin/x6100_base_patch"
FALLBACK_REPO = "9M2PJU/x6100_base_patch"
DEFAULT_USER = "root"
DEFAULT_PASS = "123"
DEFAULT_PORT = 22
DEST_DIR = "/usr/firmware"

ABOUT_TEXT = (
    "X6100 BASE firmware uploader\n\n"
    "Original firmware maintainer: gdyuldin\n"
    "(upstream x6100_base_patch DSP work)\n\n"
    "App built by: 9M2PJU\n"
    "(cross-platform uploader GUI)"
)

try:
    import paramiko  # type: ignore
    HAVE_PARAMIKO = True
except ImportError:
    HAVE_PARAMIKO = False


def app_dir():
    base = getattr(sys, "_MEIPASS", None)  # PyInstaller bundle
    if base:
        return base
    return os.path.dirname(os.path.abspath(__file__))


def fw_label(name):
    if "221112001" in name:
        return "OEM BASE 221112001 (older)"
    if "230307001" in name:
        return "OEM BASE 230307001 (newer DSP)"
    return ""


def fw_sort_key(path):
    m = re.search(r"_r(\d+)\.xgf$", os.path.basename(path))
    return int(m.group(1)) if m else 0


def display_names(paths):
    """Menu labels: newest build gets the (recommended) tag, rest get family hints."""
    if not paths:
        return []
    top = max(fw_sort_key(p) for p in paths)
    out = []
    for p in paths:
        base = os.path.basename(p)
        if fw_sort_key(p) == top:
            out.append(f"{base}  (recommended, latest)")
        else:
            lbl = fw_label(base)
            out.append(f"{base}  {lbl}".strip() if lbl else base)
    return out


def list_local_fw(extra_dirs=()):
    cands = [
        os.path.join(app_dir(), "firmwares"),
        os.path.join(os.path.dirname(app_dir()), "firmwares"),
        os.path.join(os.getcwd(), "firmwares"),
        *extra_dirs,
    ]
    out = []
    for d in cands:
        if d and os.path.isdir(d):
            for f in sorted(os.listdir(d)):
                p = os.path.join(d, f)
                if f.endswith(".xgf") and p not in out:
                    out.append(p)
            if out:
                break
    return sorted(out, key=fw_sort_key, reverse=True)


def remote_path(local_file):
    return f"{DEST_DIR}/{os.path.basename(local_file)}"


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("1.1.1.1", 80))
        return s.getsockname()[0]
    except OSError:
        return ""
    finally:
        s.close()


def tcp_ssh_open(host, port=22, timeout=0.7):
    """True if host:port accepts TCP and speaks SSH (banner check, no login)."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as s:
            s.settimeout(timeout)
            try:
                banner = s.recv(64).decode("ascii", "replace")
                return "SSH" in banner
            except (socket.timeout, OSError):
                return True  # port open even if banner slow
    except OSError:
        return False


def discover_radios(port=22, log=None, stop=None):
    """Try x6100.local/x6100, then scan our /24. Returns [ip, ...]."""
    found = []
    for h in ("x6100.local", "x6100"):
        if stop and stop.is_set():
            return found
        try:
            ip = socket.gethostbyname(h)
            if tcp_ssh_open(ip, port):
                if log:
                    log(f"found {h} -> {ip}")
                found.append(ip)
        except OSError:
            pass
    if found:
        return found
    me = lan_ip()
    if not me or me.count(".") != 3:
        if log:
            log("no LAN address, skipping subnet scan")
        return found
    prefix = me.rsplit(".", 1)[0]
    if log:
        log(f"scanning {prefix}.2-254 for SSH ...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
        futs = {ex.submit(tcp_ssh_open, f"{prefix}.{i}", port): i for i in range(2, 255)}
        for f in concurrent.futures.as_completed(futs):
            if stop and stop.is_set():
                break
            try:
                if f.result():
                    ip = f"{prefix}.{futs[f]}"
                    found.append(ip)
                    if log:
                        log(f"SSH open: {ip}")
            except OSError:
                pass
    return sorted(found, key=lambda ip: tuple(int(p) for p in ip.split(".")))


def fetch_latest_asset_url():
    for repo in (REPO, FALLBACK_REPO):
        try:
            req = urllib.request.Request(
                f"https://api.github.com/repos/{repo}/releases/latest",
                headers={"User-Agent": "x6100-uploader", "Accept": "application/vnd.github+json"},
            )
            with urllib.request.urlopen(req, timeout=20) as r:
                data = json.load(r)
            for a in data.get("assets", []):
                if a.get("name", "").endswith(".xgf"):
                    return a["browser_download_url"], a["name"]
        except OSError:
            continue
    raise RuntimeError("GitHub releases unreachable (check internet)")


def download_file(url, dest, progress_cb=None):
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)

    def hook(block, bsize, total):
        if progress_cb and total > 0:
            progress_cb(min(block * bsize * 100 // total, 100), total)

    urllib.request.urlretrieve(url, dest, reporthook=hook if progress_cb else None)
    return dest


def sftp_upload(host, port, user, password, local, remote, progress_cb):
    import paramiko  # local import, only called when available

    total = os.path.getsize(local)
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, port=port, username=user, password=password,
                   timeout=8, banner_timeout=8, auth_timeout=8)
    try:
        stdin, stdout, stderr = client.exec_command(f"mkdir -p {DEST_DIR} && ls -ld {DEST_DIR}")
        if stdout.channel.recv_exit_status() != 0:
            raise RuntimeError(stderr.read().decode() or "cannot access /usr/firmware")
        sftp = client.open_sftp()
        try:
            def cb(sent, size):
                progress_cb(sent, size or total)
            sftp.put(local, remote, callback=cb)
        finally:
            sftp.close()
        stdin, stdout, stderr = client.exec_command(f"ls -lh {remote}")
        if stdout.channel.recv_exit_status() != 0:
            raise RuntimeError("verify failed: file not on radio")
        return stdout.read().decode().strip()
    finally:
        client.close()


def scp_fallback(host, port, user, local, remote):
    dest = remote.rsplit("/", 1)[0]
    subprocess.run(["ssh", "-p", str(port), "-o", "StrictHostKeyChecking=no",
                    "-o", "UserKnownHostsFile=/dev/null", f"{user}@{host}",
                    f"mkdir -p {dest}"], check=True)
    subprocess.run(["scp", "-P", str(port), "-o", "StrictHostKeyChecking=no",
                    "-o", "UserKnownHostsFile=/dev/null", local, f"{user}@{host}:{remote}"],
                   check=True)


def upload(host, port, user, password, local, remote, progress_cb):
    """Upload via paramiko SFTP if available, else system scp. Returns verify line."""
    if HAVE_PARAMIKO:
        return sftp_upload(host, port, user, password, local, remote, progress_cb)
    scp_fallback(host, port, user, local, remote)
    return remote


DONE_TITLE = "Upload complete - now finish on the radio"


def start_checklist():
    return (
        "Before you start - you will finish the flash MANUALLY on the radio.\n"
        "The app only copies the file over WiFi.\n\n"
        "You need:\n"
        "• Radio booted WITHOUT the SD card, on your home WiFi\n"
        "  (write down its IP from the radio WiFi screen)\n"
        "• Radio on EXTERNAL power (never flash on battery)\n"
        "• Computer/phone on the SAME WiFi, SSH login root / 123\n\n"
        "After the upload YOU will:\n"
        "1. System settings → Firmware upgrade → pick the file → Update\n"
        "2. Reboot, check System info → BASE ends in rN (e.g. r13)\n"
        "3. Calibrate TX power if needed (see README)"
    )


def done_instructions(filename):
    return (
        f"Copied to /usr/firmware/{filename}.\n"
        "The radio does NOT flash itself - do this manually:\n\n"
        "1. Keep EXTERNAL power on (never flash on battery alone).\n"
        "2. On the radio: System settings → Firmware upgrade.\n"
        f"3. Pick {filename} → Update, wait until it finishes.\n"
        "4. Reboot, then check System info → BASE ends in rN (e.g. r13).\n"
        "5. TX power low / high ALC? Calibrate per README "
        "(R1CBU: TX codec gain, then per-band corr)."
    )
