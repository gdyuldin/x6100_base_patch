#!/usr/bin/env bash
# X6100 BASE firmware installer (Linux / macOS) — beginner-friendly wizard.
# Finds the radio on WiFi, lets you pick a firmware, uploads it via SCP.
# One-liner (no download needed):
#   bash <(curl -fsSL https://raw.githubusercontent.com/gdyuldin/x6100_base_patch/1.1.6/tools/install.sh)
set -euo pipefail

REPO="gdyuldin/x6100_base_patch"
DEST_DIR="/usr/firmware"
USER="root"
PORT="22"
IP=""
FILE=""
YES=0
NO_SCAN=0
LIST_ONLY=0

progress() { # progress <current> <total> <label>
  local cur=$1 total=$2 label=$3 width=20
  local filled=$(( cur * width / total ))
  local empty=$(( width - filled ))
  printf '[%s%s] %s/%s %s\n' \
    "$(printf '#%.0s' $(seq 1 $filled 2>/dev/null || echo))" \
    "$(printf -- '-%.0s' $(seq 1 $empty 2>/dev/null || echo))" \
    "$cur" "$total" "$label"
}

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Finds your X6100 on WiFi, you pick a firmware from a menu, it uploads
the .xgf to the radio via SCP. Flashing itself is done on the radio.

Options:
  -i, --ip IP        Radio IP (skip auto-find, e.g. 192.168.1.100)
  -f, --file PATH    .xgf to upload (skip menu; default: pick from menu)
  -u, --user USER    SSH user (default: root)
  -P, --port PORT    SSH port (default: 22)
  -d, --dest DIR     Remote dir (default: /usr/firmware)
      --no-scan      Don't scan the network, just ask for the IP
      --list         List bundled firmwares and exit
  -y, --yes          Accept all defaults, no questions
  -h, --help         Show this help

One-liner (nothing to download first):
  bash <(curl -fsSL https://raw.githubusercontent.com/gdyuldin/x6100_base_patch/1.1.6/tools/install.sh)

Radio SSH defaults: user 'root', password '123'.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -i|--ip) IP="${2:-}"; shift 2;;
    -f|--file) FILE="${2:-}"; shift 2;;
    -u|--user) USER="${2:-}"; shift 2;;
    -P|--port) PORT="${2:-}"; shift 2;;
    -d|--dest) DEST_DIR="${2:-}"; shift 2;;
    --no-scan) NO_SCAN=1; shift;;
    --list) LIST_ONLY=1; shift;;
    -y|--yes) YES=1; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown option: $1"; usage; exit 1;;
  esac
done

have() { command -v "$1" >/dev/null 2>&1; }
command -v scp >/dev/null || { echo "ERROR: 'scp' not found. Install OpenSSH (Linux: openssh-client, Mac: preinstalled)."; exit 1; }
command -v ssh >/dev/null || { echo "ERROR: 'ssh' not found. Install OpenSSH (Linux: openssh-client, Mac: preinstalled)."; exit 1; }

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5"

# ---------- firmware ----------

repo_root() { # prints repo root or nothing (one-liner mode has no repo)
  local d
  d="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." 2>/dev/null && pwd)" || return 0
  [ -d "$d/firmwares" ] && printf '%s' "$d"
}

list_local_fw() {
  local root
  root="$(repo_root || true)"
  [ -n "$root" ] || return 0
  local f v
  for f in "$root"/firmwares/*.xgf; do
    [ -e "$f" ] || continue
    v="$(basename "$f" | sed -n 's/.*_r\([0-9][0-9]*\)\.xgf$/\1/p')"
    printf '%010d\t%s\n' "${v:-0}" "$f"
  done | sort -rn | cut -f2-
}

fw_label() { # pretty label: which OEM BASE a file is for
  case "$(basename "$1")" in
    *221112001*) echo "for OEM BASE 221112001 (older)";;
    *230307001*) echo "for OEM BASE 230307001 (recommended)";;
    *) echo "";;
  esac
}

download_latest() {
  have curl || { echo "ERROR: no local .xgf and no 'curl' to download one. Pass --file PATH."; exit 1; }
  echo "No bundled firmware found. Downloading latest release..."
  local tmp url r
  tmp="$(mktemp -d)"
  url="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" 2>/dev/null | grep -o 'https://[^"]*\.xgf' | head -1 || true)"
  if [ -n "$url" ]; then
    echo "Downloading: $url"
    if curl -fL -o "$tmp/$(basename "$url")" "$url"; then
      FILE="$tmp/$(basename "$url")"
      return 0
    fi
  fi
  echo "ERROR: download failed. Download a .xgf by hand and pass --file PATH."
  exit 1
}

choose_firmware() {
  [ -n "$FILE" ] && return 0
  local fw=()
  while IFS= read -r f; do [ -n "$f" ] && fw+=("$f"); done < <(list_local_fw)
  if [ "${#fw[@]}" -eq 0 ]; then
    download_latest
    return 0
  fi
  if [ "$LIST_ONLY" -eq 1 ]; then
    echo "Bundled firmwares:"
    local i=1
    for f in "${fw[@]}"; do echo "  $i) $(basename "$f")  $(fw_label "$f")"; i=$((i+1)); done
    exit 0
  fi
  if [ "${#fw[@]}" -eq 1 ] || [ "$YES" -eq 1 ]; then
    FILE="${fw[0]}"
    echo "Firmware: $(basename "$FILE")  $(fw_label "$FILE")"
    return 0
  fi
  echo ""
  echo "Which firmware? (check radio: System info -> BASE date)"
  local i=1
  for f in "${fw[@]}"; do echo "  $i) $(basename "$f")  $(fw_label "$f")"; i=$((i+1)); done
  printf "Number [1]: "
  local ans
  read -r ans || true
  ans="${ans:-1}"
  if ! [[ "$ans" =~ ^[0-9]+$ ]] || [ "$ans" -lt 1 ] || [ "$ans" -gt "${#fw[@]}" ]; then
    echo "Out of range, using 1."
    ans=1
  fi
  FILE="${fw[$((ans-1))]}"
}

# ---------- radio discovery ----------

tcp_open() { # host port timeout — needs python3
  python3 -c 'import socket,sys
s=socket.socket(); s.settimeout(float(sys.argv[3]))
sys.exit(0 if s.connect_ex((sys.argv[1], int(sys.argv[2])))==0 else 1)' \
    "$1" "$2" "${3:-1}" 2>/dev/null
}

ssh_alive() { # host — true if a real SSH server answers (password still needed later)
  local out
  out="$(ssh -p "$PORT" -o BatchMode=yes $SSH_OPTS "$USER@$1" true 2>&1 || true)"
  echo "$out" | grep -qi "permission denied"
}

my_lan_ip() {
  if have ip; then
    ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.*src \([0-9.]*\).*/\1/p' | head -1
  elif have ipconfig; then
    ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true
  else
    hostname -I 2>/dev/null | tr ' ' '\n' | grep -m1 -E '^192\.168\.|^10\.|^172\.(1[6-9]|2[0-9]|3[0-3])\.' || true
  fi
}

scan_lan() { # prints "ip" lines with SSH (port 22) open on our /24
  local me prefix
  me="$(my_lan_ip || true)"
  [ -n "$me" ] || return 0
  prefix="${me%.*}"
  seq 2 254 | xargs -P 64 -I@@ sh -c \
    "python3 -c 'import socket,sys;s=socket.socket();s.settimeout(0.6);sys.exit(0 if s.connect_ex((\"$prefix.@@\",$PORT))==0 else 1)' 2>/dev/null && echo $prefix.@@" \
    2>/dev/null || true
}

choose_ip() {
  [ -n "$IP" ] && return 0
  local found=()
  if [ "$NO_SCAN" -ne 1 ] && have python3; then
    echo ""
    echo "Looking for your X6100 on this WiFi (takes ~10 seconds)..."
    local cands=() h
    for h in x6100.local x6100; do
      if tcp_open "$h" "$PORT" 1; then cands+=("$h"); fi
    done
    if [ "${#cands[@]}" -eq 0 ]; then
      while IFS= read -r h; do [ -n "$h" ] && cands+=("$h"); done < <(scan_lan)
    fi
    for h in ${cands[@]+"${cands[@]}"}; do
      if ssh_alive "$h"; then found+=("$h"); fi
    done
  fi
  if [ "${#found[@]}" -eq 1 ]; then
    IP="${found[0]}"
    echo "Found your radio at: $IP"
    return 0
  fi
  if [ "${#found[@]}" -gt 1 ]; then
    echo "Found several devices. Pick your radio (check its WiFi screen for the IP):"
    local i=1
    for h in "${found[@]}"; do echo "  $i) $h"; i=$((i+1)); done
    echo "  0) type it myself"
    printf "Number [1]: "
    local ans
    read -r ans || true
    ans="${ans:-1}"
    if [ "$ans" = "0" ]; then
      IP=""
    elif [[ "$ans" =~ ^[0-9]+$ ]] && [ "$ans" -ge 1 ] && [ "$ans" -le "${#found[@]}" ]; then
      IP="${found[$((ans-1))]}"
      return 0
    else
      IP="${found[0]}"
      return 0
    fi
  else
    [ "$NO_SCAN" -eq 1 ] || echo "Auto-find came up empty (radio off WiFi, or no python3 for scanning)."
  fi
  while [ -z "$IP" ]; do
    echo ""
    echo "Type the radio IP from its WiFi settings screen (e.g. 192.168.1.100):"
    printf "> "
    read -r IP || true
    if ! [[ "$IP" =~ ^[A-Za-z0-9._-]+$ ]]; then
      echo "That doesn't look like an IP or hostname, try again."
      IP=""
    fi
  done
}

# ---------- main ----------

progress 1 4 "picking firmware"
choose_firmware
[ -f "$FILE" ] || { echo "ERROR: file not found: $FILE"; exit 1; }
case "$FILE" in *.xgf) ;; *) echo "WARNING: '$FILE' is not a .xgf — continuing anyway.";; esac

progress 2 4 "finding radio"
choose_ip

FNAME="$(basename "$FILE")"
echo ""
echo "Radio:   $USER@$IP:$PORT"
echo "File:    $FILE"
echo "Dest:    $DEST_DIR/$FNAME"
if [ "$YES" -ne 1 ]; then
  printf "Upload? [Y/n] "
  read -r ans || true
  case "$ans" in [nN]*) echo "Aborted."; exit 0;; esac
fi

# shellcheck disable=SC2086
progress 3 4 "uploading via SCP (password '123' if asked; typing shows nothing)"
if ! ssh -p "$PORT" $SSH_OPTS "$USER@$IP" "mkdir -p $DEST_DIR && ls -ld $DEST_DIR"; then
  echo "ERROR: cannot reach $USER@$IP. Check:"
  echo "  - radio and computer on the SAME WiFi, radio booted WITHOUT SD card"
  echo "  - IP matches the radio WiFi screen (re-run without --ip to rescan)"
  echo "  - password '123' unless you changed it"
  exit 1
fi
scp -P "$PORT" $SSH_OPTS "$FILE" "$USER@$IP:$DEST_DIR/$FNAME"

progress 4 4 "verifying"
# shellcheck disable=SC2086
ssh -p "$PORT" $SSH_OPTS "$USER@$IP" "ls -lh $DEST_DIR/$FNAME"

cat <<EOF

Done. Now on the radio:
  1. External power on (never flash on battery alone).
  2. System settings -> Firmware upgrade -> choose $FNAME -> Update.
  3. Reboot, System info: BASE version should end in rX (e.g. r13).
  4. TX power off? Calibrate per README "TX calibration".
EOF
