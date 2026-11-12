# X6100 BASE firmware installer (Windows, PowerShell 5.1+) — beginner wizard.
# Finds the radio on WiFi, you pick a firmware, it uploads via SCP.
# One-liner (nothing to download first) — paste into PowerShell:
#   $f="$env:TEMP\x6100-install.ps1"; irm https://raw.githubusercontent.com/gdyuldin/x6100_base_patch/1.1.6/tools/install.ps1 -OutFile $f; & $f
param(
  [string]$Ip = "",
  [string]$File = "",
  [string]$User = "root",
  [int]$Port = 22,
  [string]$Dest = "/usr/firmware",
  [switch]$NoScan,
  [switch]$List,
  [switch]$Yes,
  [switch]$Help
)

$Repo = "gdyuldin/x6100_base_patch"
$SshOpts = @("-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "-o", "ConnectTimeout=5")

function Show-ProgressBar($Cur, $Total, $Label) {
  $w = 20
  $f = [int]($Cur * $w / $Total)
  $bar = ("#" * $f).PadRight($w, "-")
  Write-Host ("[{0}] {1}/{2} {3}" -f $bar, $Cur, $Total, $Label)
}

function Usage {
  @"
Usage: .\install.ps1 [-Ip 192.168.1.100] [-File path\to\fw.xgf]

Finds your X6100 on WiFi, you pick a firmware from a menu, it uploads
the .xgf to the radio via SCP. Flashing itself is done on the radio.

  -Ip     Radio IP (skip auto-find)
  -File   .xgf to upload (skip menu)
  -User   SSH user (default: root), -Port (default: 22)
  -NoScan Skip network scan, just ask for the IP
  -List   List bundled firmwares and exit
  -Yes    Accept all defaults, no questions

Easiest: double-click tools\install.bat (no terminal needed), or one-liner:
  `$f="$env:TEMP\x6100-install.ps1"; irm https://raw.githubusercontent.com/gdyuldin/x6100_base_patch/1.1.6/tools/install.ps1 -OutFile $f; & $f

Radio SSH defaults: user 'root', password '123'.
"@
}

if ($Help) { Usage; exit 0 }

if (-not (Get-Command scp -ErrorAction SilentlyContinue)) {
  Write-Error "'scp' not found. Install OpenSSH Client: Settings > Apps > Optional features > OpenSSH Client."
  exit 1
}

function Test-TcpOpen($HostName, $TcpPort, $TimeoutMs = 900) {
  $c = New-Object Net.Sockets.TcpClient
  try {
    $r = $c.BeginConnect($HostName, $TcpPort, $null, $null)
    if ($r.AsyncWaitHandle.WaitOne($TimeoutMs)) { $c.EndConnect($r); return $true }
    return $false
  } catch { return $false } finally { $c.Close() }
}

function Test-SshAlive($HostName) {
  # True if a real SSH server answers (wrong/no key -> "Permission denied", not timeout/refused).
  $out = & ssh -p $Port -o BatchMode=yes @SshOpts "$User@$HostName" true 2>&1 | Out-String
  return ($out -match "Permission denied")
}

function Get-LanPrefix {
  $ip = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -match '^(192\.168|10|172\.(1[6-9]|2[0-9]|3[0-1]))\.' -and $_.IPAddress -ne '127.0.0.1' } |
    Select-Object -First 1 -ExpandProperty IPAddress
  if ($ip) { return ($ip -replace '\.\d+$', '') }
  return $null
}

function Find-Radio {
  $cands = @()
  foreach ($h in @("x6100.local", "x6100")) {
    if (Test-TcpOpen $h $Port) { $cands += $h }
  }
  if ($cands.Count -eq 0) {
    $prefix = Get-LanPrefix
    if ($prefix) {
      Write-Host "Scanning $prefix.2-254 for the radio (takes ~10 seconds)..."
      $clients = @()
      foreach ($i in 2..254) {
        $c = New-Object Net.Sockets.TcpClient
        try { $r = $c.BeginConnect("$prefix.$i", $Port, $null, $null); $clients += [pscustomobject]@{ Ip = "$prefix.$i"; C = $c; R = $r } }
        catch { $c.Close() }
      }
      Start-Sleep -Milliseconds 1600
      foreach ($t in $clients) {
        try { if ($t.C.Connected) { $cands += $t.Ip } } catch { }
        $t.C.Close()
      }
    }
  }
  $alive = @()
  foreach ($h in $cands) { if (Test-SshAlive $h) { $alive += $h } }
  return $alive
}

function Get-FwLabel($Name) {
  if ($Name -match "221112001") { return "for OEM BASE 221112001 (older)" }
  if ($Name -match "230307001") { return "for OEM BASE 230307001 (recommended)" }
  return ""
}

# ---------- firmware ----------
Show-ProgressBar 1 4 "picking firmware"

if ([string]::IsNullOrWhiteSpace($File)) {
  $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
  $fw = @()
  if ($scriptDir) {
    $fw = @(Get-ChildItem (Join-Path (Split-Path -Parent $scriptDir) "firmwares\*.xgf") -ErrorAction SilentlyContinue |
      Sort-Object { if ($_.Name -match '_r(\d+)\.xgf$') { [int]$Matches[1] } else { 0 } } -Descending)
  }
  if ($fw.Count -eq 0) {
    Write-Host "No bundled firmware found. Downloading latest release..."
    $url = $null
    try {
      $rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -TimeoutSec 20
      $asset = $rel.assets | Where-Object { $_.name -like "*.xgf" } | Select-Object -First 1
      if ($asset) { $url = $asset.browser_download_url }
    } catch { }
    if (-not $url) { Write-Error "Download failed. Download a .xgf by hand and re-run with -File PATH."; exit 1 }
    $tmp = Join-Path $env:TEMP ([System.IO.Path]::GetFileName($url))
    Write-Host "Downloading: $url"
    Invoke-WebRequest $url -OutFile $tmp
    $File = $tmp
  } elseif ($List) {
    Write-Host "Bundled firmwares:"
    $i = 1
    foreach ($f in $fw) { Write-Host ("  {0}) {1}  {2}" -f $i, $f.Name, (Get-FwLabel $f.Name)); $i++ }
    exit 0
  } elseif ($fw.Count -eq 1 -or $Yes) {
    $File = $fw[0].FullName
    Write-Host ("Firmware: {0}  {1}" -f $fw[0].Name, (Get-FwLabel $fw[0].Name))
  } else {
    Write-Host ""
    Write-Host "Which firmware? (check radio: System info -> BASE date)"
    $i = 1
    foreach ($f in $fw) { Write-Host ("  {0}) {1}  {2}" -f $i, $f.Name, (Get-FwLabel $f.Name)); $i++ }
    $ans = Read-Host "Number [1]"
    if ([string]::IsNullOrWhiteSpace($ans)) { $ans = "1" }
    $n = 0
    if (-not ([int]::TryParse($ans, [ref]$n)) -or $n -lt 1 -or $n -gt $fw.Count) {
      Write-Host "Out of range, using 1."; $n = 1
    }
    $File = $fw[$n - 1].FullName
  }
}

if (-not (Test-Path $File)) { Write-Error "File not found: $File"; exit 1 }
if ($File -notlike "*.xgf") { Write-Warning "$File is not a .xgf - continuing anyway." }

# ---------- radio ----------
Show-ProgressBar 2 4 "finding radio"

if ([string]::IsNullOrWhiteSpace($Ip) -and -not $NoScan) {
  Write-Host "Looking for your X6100 on this WiFi..."
  $found = @(Find-Radio)
  if ($found.Count -eq 1) {
    $Ip = $found[0]
    Write-Host "Found your radio at: $Ip"
  } elseif ($found.Count -gt 1) {
    Write-Host "Found several devices. Pick your radio (check its WiFi screen for the IP):"
    $i = 1
    foreach ($h in $found) { Write-Host "  $i) $h"; $i++ }
    Write-Host "  0) type it myself"
    $ans = Read-Host "Number [1]"
    if ([string]::IsNullOrWhiteSpace($ans)) { $ans = "1" }
    if ($ans -eq "0") { $Ip = "" }
    else {
      $n = 0
      if ([int]::TryParse($ans, [ref]$n) -and $n -ge 1 -and $n -le $found.Count) { $Ip = $found[$n - 1] }
      else { $Ip = $found[0] }
    }
  } else {
    Write-Host "Auto-find came up empty (radio off WiFi?). You'll type the IP by hand."
  }
}
while ([string]::IsNullOrWhiteSpace($Ip)) {
  Write-Host ""
  $Ip = Read-Host "Radio IP from its WiFi settings screen (e.g. 192.168.1.100)"
  if ($Ip -notmatch '^[A-Za-z0-9._-]+$') { Write-Host "That doesn't look like an IP or hostname, try again."; $Ip = "" }
}

$fname = Split-Path $File -Leaf
Write-Host ""
Write-Host "Radio: $User@${Ip}:$Port"
Write-Host "File:  $File"
Write-Host "Dest:  $Dest/$fname"
if (-not $Yes) {
  $ans = Read-Host "Upload? [Y/n]"
  if ($ans -match '^[nN]') { Write-Host "Aborted."; exit 0 }
}

Show-ProgressBar 3 4 "uploading via SCP (password '123' if asked; typing shows nothing)"
& ssh -p $Port @SshOpts "$User@$Ip" "mkdir -p $Dest && ls -ld $Dest"
if ($LASTEXITCODE -ne 0) {
  Write-Error @"
Cannot reach $User@$Ip. Check:
  - radio and computer on the SAME WiFi, radio booted WITHOUT SD card
  - IP matches the radio WiFi screen (re-run to rescan)
  - password '123' unless you changed it
"@
  exit 1
}
& scp -P $Port @SshOpts $File "$User@${Ip}:$Dest/$fname"
if ($LASTEXITCODE -ne 0) { Write-Error "SCP upload failed."; exit 1 }

Show-ProgressBar 4 4 "verifying"
& ssh -p $Port @SshOpts "$User@$Ip" "ls -lh $Dest/$fname"

@"

Done. Now on the radio:
  1. External power on (never flash on battery alone).
  2. System settings -> Firmware upgrade -> choose $fname -> Update.
  3. Reboot, System info: BASE version should end in rX (e.g. r13).
  4. TX power off? Calibrate per README "TX calibration".
"@
