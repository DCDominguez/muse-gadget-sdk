<#
.SYNOPSIS
  Flash a Muse test build onto the Tab5 with your SDK token (plan.md 1.2).

.DESCRIPTION
  Wraps tab5_flash.py. The token comes from tab5-sdk-token.txt (saved by
  tab5-sdk-token.html, in this folder or Downloads); without one, a masked
  window asks for it. The token goes to the Python script on stdin or as that
  file, is never printed, and the file is deleted after flashing.

  Refuses unless: the kit's files match SHA256SUMS.txt, the board on the port
  is an ESP32-P4 v1.x with the expected MAC, and a 16 MB backup of it exists
  in ~\tab5-backups. Never erases the chip, burns eFuses or writes the C6.

.EXAMPLE
  .\tab5-flash.ps1 -Port COM4 -First    # first flash over the old firmware
  .\tab5-flash.ps1 -Port COM4           # later: app only, keeps pairing and Wi-Fi
#>
param(
  [Parameter(Mandatory = $true)][string]$Port,
  [switch]$First,
  [int]$LogSecs = 60
)
$ErrorActionPreference = 'Stop'
$Kit = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Kit

& python -m esptool version *> $null
if ($LASTEXITCODE -ne 0) { Write-Host "STOP: python -m esptool isn't working" -ForegroundColor Red; exit 1 }

$tokenFile = @(
  (Join-Path $Kit 'tab5-sdk-token.txt'),
  (Join-Path $HOME 'Downloads\tab5-sdk-token.txt')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$keyFile = @(
  (Join-Path $Kit 'tab5-elevenlabs-key.txt'),
  (Join-Path $HOME 'Downloads\tab5-elevenlabs-key.txt')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$pyArgs = @('tab5_flash.py', '--port', $Port, '--log-secs', "$LogSecs")
if ($First) { $pyArgs += '--first' }

if ($tokenFile) {
  Write-Host "Token: from $tokenFile (deleted after flashing)"
  if ($keyFile) {
    Write-Host "ElevenLabs key: from $keyFile (deleted after flashing)"
    & python @pyArgs --token-file $tokenFile --key-file $keyFile
  } else {
    Write-Host "No ElevenLabs key: replies stay text"
    & python @pyArgs --token-file $tokenFile
  }
} else {
  Add-Type -AssemblyName System.Windows.Forms, System.Drawing
  $f = New-Object Windows.Forms.Form
  $f.Text = 'Muse SDK key for the Tab5'; $f.Width = 520; $f.Height = 230; $f.TopMost = $true
  $f.StartPosition = 'CenterScreen'; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false
  $l = New-Object Windows.Forms.Label
  $l.Text = 'Paste your SDK token (gadgets.muse.ai > Account > SDK tokens):'
  $l.SetBounds(12, 12, 480, 20)
  $t = New-Object Windows.Forms.TextBox
  $t.UseSystemPasswordChar = $true; $t.SetBounds(12, 38, 480, 24)
  $l2 = New-Object Windows.Forms.Label
  $l2.Text = 'Optional, for spoken replies: your ElevenLabs API key'
  $l2.SetBounds(12, 72, 480, 20)
  $k = New-Object Windows.Forms.TextBox
  $k.UseSystemPasswordChar = $true; $k.SetBounds(12, 98, 480, 24)
  $ok = New-Object Windows.Forms.Button
  $ok.Text = 'Flash'; $ok.SetBounds(332, 140, 75, 28); $ok.DialogResult = 'OK'
  $no = New-Object Windows.Forms.Button
  $no.Text = 'Cancel'; $no.SetBounds(417, 140, 75, 28); $no.DialogResult = 'Cancel'
  $f.Controls.AddRange(@($l, $t, $l2, $k, $ok, $no)); $f.AcceptButton = $ok; $f.CancelButton = $no
  if ($f.ShowDialog() -ne 'OK' -or -not $t.Text) { Write-Host 'Cancelled: nothing written.'; exit 1 }
  $token = $t.Text.Trim(); $key = $k.Text.Trim(); $t.Text = ''; $k.Text = ''; $f.Dispose()
  @($token, $key) | & python @pyArgs
  Remove-Variable token, key
}
exit $LASTEXITCODE
