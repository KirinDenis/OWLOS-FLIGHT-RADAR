# flash-and-check.ps1: one step of the update round trip. After flashing this version without erase, does
# the board start, reach the network, and keep every setting it had?
#
# Run (COM5 and the MAC are examples; the MAC comes from your own notes):
#   & .\cfg-snapshot.ps1 -Port COM5 -Out cfg-COM5-before.json          once, before the first flash
#   & .\flash-and-check.ps1 -Dir public\fw\st7796 -Port COM5 -Chip esp32 -Mac aa:bb:cc:dd:ee:ff -Before cfg-COM5-before.json
# For a beta: the beta, then the live stable over it, then the beta again, this rig each time.
# Needs: Windows PowerShell 5.1, bench-serial.ps1, net-diag.ps1 and cfg-snapshot.ps1 beside it,
#   flash-manifest.ps1 from esp32-flash-and-settings/files/rigs (found by its place in this collection, or
#   give -Flasher), esptool (flash-manifest.ps1 takes -Python), a site folder with manifest.json and its
#   parts (fw/<board>/ or fw/<version>/<board>/ as the site publishes them).
# Changes on the board: its firmware. flash-manifest.ps1 writes the manifest's parts at the manifest's
#   offsets, as the browser flasher does, with no erase, so the settings stay, and only after the chip and
#   the MAC matched. Nothing else is written. Back the board up first (backup-board.ps1) if it holds
#   anything of value.
# Prints: the flasher's line (how many parts verified), the start as net-diag.ps1 prints it (statuses and
#   numbers only), then which settings differ from the saved file (field names only). The pass: every
#   part verified, the WiFi up, the requests answered, "differ: none".
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Mac,
    [Parameter(Mandatory = $true)][string]$Chip,
    [Parameter(Mandatory = $true)][string]$Before,
    [int]$Listen = 90,
    [string]$Flasher = '',
    [string]$Python = ''
)
if (-not $Flasher) {
    $Flasher = Join-Path $PSScriptRoot '..\..\..\esp32-flash-and-settings\files\rigs\flash-manifest.ps1'
}
if (-not (Test-Path $Flasher)) {
    "NOT FLASHED: no flash-manifest.ps1 at $Flasher, give -Flasher"
    exit 1
}
if (-not (Test-Path $Before)) {
    "NOT FLASHED: no saved settings at $Before, make them first with cfg-snapshot.ps1 -Out"
    exit 1
}
$fa = @{ Dir = $Dir; Port = $Port; Chip = $Chip; Mac = $Mac }
if ($Python) {
    $fa.Python = $Python
}
& $Flasher @fa
# the flasher ends with exit 1 on any refusal or a part not verified; on success its last command is
# esptool, which leaves 0
$ok = $? -and $LASTEXITCODE -eq 0
if (-not $ok) {
    "${Port}: the flash did not pass, nothing more done"
    exit 1
}
& (Join-Path $PSScriptRoot 'net-diag.ps1') -Port $Port -Seconds $Listen
& (Join-Path $PSScriptRoot 'cfg-snapshot.ps1') -Port $Port -Before $Before
