# build one screen's firmware and flash it, only when the build is new and whole and the board is
# the one meant: its chip and its MAC, both. the MAC comes from your own notes, kept outside the repository
#   flash-board.ps1 -Repo <repo> -App my-project -Disp st7796 -Port <port> -Chip esp32 -Mac aa:bb:cc:dd:ee:ff
#   -NoFlash: only build
# from OWLOS Flight Radar's working folder. run it in PowerShell: BUILD.BAT goes through cmd /c
param([string]$Repo, [string]$App, [string]$Disp, [string]$Port, [string]$Chip, [string]$Mac, [switch]$NoFlash,
      [string]$Python = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe")
$dir = if ($Disp -eq "ili9341") { "build" } else { "build_$Disp" }
$bin = "$Repo\firmware\$dir\$App.bin"
$out = cmd /c "$Repo\firmware\BUILD.BAT $Disp" 2>&1
$code = $LASTEXITCODE
$warn = @($out | Select-String -Pattern "warning:|error:")
"build $Disp : exit $code, $($warn.Count) warnings/errors"
$warn | Select-Object -First 8 | ForEach-Object { "  $_" }
# a failed build leaves the old .bin in place, and esptool flashes it happily
$done = $out | Select-String "Project build complete"
if ($code -ne 0 -or -not $done -or -not (Test-Path $bin) -or $warn.Count -gt 0) {
    "NOT FLASHED: the build did not finish"
    exit 1
}
$size = (Get-Item $bin).Length
"app $size bytes, built $((Get-Item $bin).LastWriteTime)"
if ($NoFlash) { exit 0 }
# COM numbers move: the chip and the MAC say which board this port really is. asked with no --chip, so
# esptool finds the chip itself; no answer, no MAC line or no chip line is a refusal (as flash-one.ps1)
$o = (& $Python -m esptool -p $Port chip_id 2>&1 | Out-String)
$seenMac = if ($o -match 'MAC:\s*([0-9a-f:]{17})') { $Matches[1] } else { '' }
$seenChip = if ($o -match 'Chip is (ESP32[^\s(]*)') { $Matches[1] } else { '' }
$want = @{ 'esp32' = '^ESP32-D0WD'; 'esp32c3' = '^ESP32-C3'; 'esp32s3' = '^ESP32-S3';
           'esp32c5' = '^ESP32-C5'; 'esp32p4' = '^ESP32-P4' }[$Chip]
if (-not ($seenMac -ne '' -and $seenMac -eq $Mac.ToLower() -and $seenChip -ne '' -and $want -and $seenChip -match $want)) {
    "NOT FLASHED: $Port has chip '$seenChip' MAC '$seenMac', meant $Chip $Mac"
    exit 1
}
Push-Location "$Repo\firmware\$dir"
$f = & $Python -m esptool --chip $Chip -p $Port -b 460800 --before default_reset --after hard_reset write_flash "@flash_args" 2>&1
Pop-Location
$ok = @($f | Select-String "Hash of data verified").Count
"flashed $Port ($seenChip $seenMac): $ok of 3 verified"
if ($ok -ne 3) { $f | Select-Object -Last 10; exit 1 }
