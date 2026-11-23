# flash a site folder (public/fw/<board>) the way the browser flasher does: the manifest's parts at the
# manifest's offsets, nothing else touched. only the board meant (its chip and its MAC), every part verified.
#   flash-manifest.ps1 -Dir public\fw\st7796 -Port <port> -Chip esp32 -Mac aa:bb:cc:dd:ee:ff
#   -Erase    "yes to erase" on the page: the whole chip first
#   -Family   which build, for a manifest with one a chip (ESP32-S3); without it, the first
#   -Backup   a file: read the whole flash into it first, if it is not there yet
#   -Restore  a file: write it back at 0 and stop
# chip and MAC checked before anything. from OWLOS Flight Radar's and ESP32 Web Flasher's working folders
param([string]$Dir, [string]$Port, [string]$Chip, [string]$Mac, [string]$Family, [string]$Backup, [string]$Restore, [switch]$Erase,
      [string]$Python = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe")
$py = $Python
# asked with no --chip: esptool finds the chip itself. any doubt is a refusal (as flash-one.ps1)
$o = (& $py -m esptool -p $Port chip_id 2>&1 | Out-String)
$seenMac = if ($o -match 'MAC:\s*([0-9a-f:]{17})') { $Matches[1] } else { '' }
$seenChip = if ($o -match 'Chip is (ESP32[^\s(]*)') { $Matches[1] } else { '' }
$want = @{ 'esp32' = '^ESP32-D0WD'; 'esp32c3' = '^ESP32-C3'; 'esp32s3' = '^ESP32-S3';
           'esp32c5' = '^ESP32-C5'; 'esp32p4' = '^ESP32-P4'; 'esp32s2' = '^ESP32-S2'; 'esp32c6' = '^ESP32-C6' }[$Chip]
if (-not ($seenMac -ne '' -and $seenMac -eq $Mac.ToLower() -and $seenChip -ne '' -and $want -and $seenChip -match $want)) {
    "NOT FLASHED: $Port has chip '$seenChip' MAC '$seenMac', meant $Chip $Mac"
    exit 1
}
if ($Backup) {
    if (Test-Path $Backup) { "backup already there: $Backup ($((Get-Item $Backup).Length) bytes)" }
    else {
        # 460800: a CH340 fails reads at 921600
        $r = & $py -m esptool --chip $Chip -p $Port -b 460800 read_flash 0 ALL $Backup 2>&1
        if (-not (Test-Path $Backup)) { $r | Select-Object -Last 8; exit 1 }
        "backup $Backup, $((Get-Item $Backup).Length) bytes"
    }
}
if ($Restore) {
    $f = & $py -m esptool --chip $Chip -p $Port -b 460800 --after hard_reset write_flash 0 $Restore 2>&1
    $ok = @($f | Select-String "Hash of data verified").Count
    "restored $Port from $Restore : $ok of 1 verified"
    if ($ok -ne 1) { $f | Select-Object -Last 10; exit 1 }
    exit 0
}
$m = Get-Content -Raw "$Dir\manifest.json" | ConvertFrom-Json
$b = if ($Family) { $m.builds | Where-Object { $_.chipFamily -eq $Family } } else { $m.builds[0] }
$parts = $b.parts
if ($Erase) {
    $e = & $py -m esptool --chip $Chip -p $Port erase_flash 2>&1
    if (-not ($e | Select-String "erased successfully|Chip erase completed")) { "ERASE FAILED"; $e | Select-Object -Last 5; exit 1 }
    "erased $Port"
}
$args2 = @()
foreach ($p in $parts) { $args2 += ('0x{0:x}' -f [int]$p.offset); $args2 += (Join-Path $Dir $p.path) }
$f = & $py -m esptool --chip $Chip -p $Port -b 460800 --before default_reset --after hard_reset write_flash @args2 2>&1
$ok = @($f | Select-String "Hash of data verified").Count
"flashed $Port ($seenChip $seenMac) from $(Split-Path -Leaf $Dir), manifest $($m.version): $ok of $($parts.Count) verified"
if ($ok -ne $parts.Count) { $f | Select-Object -Last 10; exit 1 }
