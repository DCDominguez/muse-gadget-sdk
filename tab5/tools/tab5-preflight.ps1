<#
.SYNOPSIS
  Read-only Tab5 preflight (plan.md, Phase 1.0). Run from an ESP-IDF PowerShell
  (or any shell where `python -m esptool` works).

.DESCRIPTION
  What it does:
    1. Lists serial ports and picks yours (or use -Port COMx).
    2. Confirms the chip is an ESP32-P4 and the flash is 16 MB.
    3. Reads the MAC.
    4. Backs up the full 16 MB flash to a file OUTSIDE your repo, then reads it
       a second time and compares SHA256 hashes to prove the backup is good.
    5. Writes a short report next to the backup.

  What it NEVER does: write-flash, erase-flash, write to eFuses, touch the C6,
  or print your SDK token. The only file it creates is the backup + report.

  STOPS (exit non-zero) if: no esptool, no/many ports without -Port, chip is not
  ESP32-P4, flash is not 16 MB, the backup file already exists, the size is
  wrong, or the two reads differ.

  NOT covered here (do these separately, see plan.md): display revision, the
  C6 firmware version (needs P4 firmware to query it), download-mode procedure.

.EXAMPLE
  .\tab5-preflight.ps1
  .\tab5-preflight.ps1 -Port COM5
  .\tab5-preflight.ps1 -Port COM5 -SkipVerifyRead   # faster, weaker proof
#>
param(
  [string]$Port,
  [string]$BackupDir = (Join-Path $HOME 'tab5-backups'),
  [int]$Baud = 460800,
  [switch]$SkipVerifyRead
)

$ErrorActionPreference = 'Stop'
$FlashBytes = 16777216      # 16 MB
$FlashHex   = '0x1000000'

function Stop-Preflight([string]$msg) {
  Write-Host "`nSTOP: $msg" -ForegroundColor Red
  exit 1
}

function Invoke-Esptool([string[]]$EspArgs) {
  $ErrorActionPreference = 'Continue'   # native stderr must not throw
  Write-Host ("> python -m esptool " + ($EspArgs -join ' ')) -ForegroundColor DarkGray
  $out = & python -m esptool @EspArgs 2>&1 | Out-String
  $code = $LASTEXITCODE
  Write-Host $out
  if ($code -ne 0) {
    Stop-Preflight "esptool failed (exit $code). If it cannot connect, the Tab5 may need its download-mode button sequence (check M5's docs). Do not flash anything yet."
  }
  return $out
}

# 0. esptool present?
try {
  $ErrorActionPreference = 'Continue'
  $v = & python -m esptool version 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { throw "no esptool" }
  Write-Host "esptool: $($v.Trim())"
} catch {
  Stop-Preflight "python -m esptool not working. Install/activate ESP-IDF v6.0.1 tools first."
}

# 1. Port
if (-not $Port) {
  $found = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match '\(COM\d+\)' })
  Write-Host "Serial ports found: $($found.Count)"
  $found | ForEach-Object { Write-Host "  $($_.FriendlyName)   [$($_.InstanceId)]" }
  if ($found.Count -eq 0) {
    Stop-Preflight "No serial ports. Try a data-capable USB cable, check Device Manager > Ports, and the board's download-mode sequence."
  }
  if ($found.Count -gt 1) {
    Stop-Preflight "More than one port. Re-run with -Port COMx for the Tab5 (unplug/replug to see which one appears)."
  }
  $Port = [regex]::Match($found[0].FriendlyName, 'COM\d+').Value
}
Write-Host "Using port: $Port`n"

# 2. Chip + flash
$chip = Invoke-Esptool @('-p', $Port, '-b', "$Baud", 'chip-id')
if ($chip -notmatch 'ESP32-P4') { Stop-Preflight "Chip is not an ESP32-P4. Check the output above; do not continue." }
$rev = [regex]::Match($chip, 'revision\s+(v[\d\.]+)').Groups[1].Value

$flash = Invoke-Esptool @('-p', $Port, '-b', "$Baud", 'flash-id')
$mb = [regex]::Match($flash, 'Detected flash size:\s*(\d+)\s*MB').Groups[1].Value
if ($mb -ne '16') { Stop-Preflight "Flash is '$mb' MB, expected 16 MB. Report this before continuing." }

$macOut = Invoke-Esptool @('-p', $Port, '-b', "$Baud", 'read-mac')
$mac = [regex]::Match($macOut, 'MAC:\s*([0-9a-fA-F:]{17})').Groups[1].Value
if (-not $mac) { Stop-Preflight "Could not read the MAC address." }
$macTag = $mac.Replace(':', '').ToLower()

# 3. Backup (never overwrite)
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $BackupDir "tab5-$macTag-$stamp.bin"
if (Test-Path $backup) { Stop-Preflight "Backup file already exists: $backup" }

Write-Host "Backing up 16 MB flash to $backup (a few minutes)..." -ForegroundColor Cyan
Invoke-Esptool @('--chip', 'esp32p4', '-p', $Port, '-b', "$Baud", 'read-flash', '0', $FlashHex, $backup) | Out-Null

$size = (Get-Item $backup).Length
if ($size -ne $FlashBytes) { Stop-Preflight "Backup is $size bytes, expected $FlashBytes. Do not use it." }
$hash1 = (Get-FileHash $backup -Algorithm SHA256).Hash

$verifyNote = 'second read skipped'
if (-not $SkipVerifyRead) {
  $verify = Join-Path $BackupDir "tab5-$macTag-$stamp.verify.bin"
  Write-Host "Reading a second time to verify..." -ForegroundColor Cyan
  Invoke-Esptool @('--chip', 'esp32p4', '-p', $Port, '-b', "$Baud", 'read-flash', '0', $FlashHex, $verify) | Out-Null
  $hash2 = (Get-FileHash $verify -Algorithm SHA256).Hash
  if ($hash1 -ne $hash2) {
    Stop-Preflight "The two reads differ ($hash1 vs $hash2). Files kept for inspection. Try a lower -Baud (e.g. 115200) or another cable/port. Do NOT flash until a backup verifies."
  }
  Remove-Item $verify
  $verifyNote = 'two independent reads matched'
}
Set-ItemProperty -Path $backup -Name IsReadOnly -Value $true

# 4. Report
$report = Join-Path $BackupDir "tab5-$macTag-$stamp.report.txt"
@"
Tab5 preflight report ($stamp)
Port:        $Port
Chip:        ESP32-P4 $rev
Flash:       16 MB
MAC:         $mac
Backup file: $backup
Size:        $size bytes
SHA256:      $hash1
Verification: $verifyNote

Restore (only if needed, from the stock backup):
  python -m esptool --chip esp32p4 -p $Port -b $Baud write-flash 0 "$backup"

Still to do (not covered by this script): display revision, C6 firmware
version (read-only), download-mode procedure. See plan.md Gate 1.0.
"@ | Set-Content -Path $report -Encoding UTF8

Write-Host "`nDONE. Report: $report" -ForegroundColor Green
Write-Host "Send the report contents (not the .bin) to Claude Code / me for Gate 1.0 review."
Write-Host "Nothing was written to the Tab5."
