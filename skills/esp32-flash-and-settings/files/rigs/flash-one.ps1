# flash a finished build to one board, only when the board is the one meant. no erase: the settings stay.
# the board is asked with the chip detected, not assumed: both its chip and its MAC must be the ones
# given, and any doubt (no answer, no MAC line, no chip line) is a refusal
#   flash-one.ps1 -Dir <repo>\firmware\build_st7796 -Port <port> -Chip esp32 -Mac aa:bb:cc:dd:ee:ff
# -Dir is a build folder: the build writes flash_args there, and its paths are relative to it.
# the MAC comes from your own list of boards, kept outside the repository.
# -Python: IDF's own Python, by default the one ESP-IDF 5.5's installer makes.
# made for esptool 4.10's words ("Chip is ESP32-...", "MAC: ..."): a newer esptool that words them
# otherwise makes this refuse, which is the safe side
param([string]$Dir, [string]$Port, [string]$Mac, [string]$Chip,
      [string]$Python = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe")
if (-not (Test-Path "$Dir\flash_args")) { "NOT FLASHED: no flash_args in $Dir"; exit 1 }
# no --chip here: esptool finds the chip itself, so a wrong one shows instead of being assumed
$o = (& $Python -m esptool -p $Port chip_id 2>&1 | Out-String)
$seenMac = if ($o -match 'MAC:\s*([0-9a-f:]{17})') { $Matches[1] } else { '' }
$seenChip = if ($o -match 'Chip is (ESP32[^\s(]*)') { $Matches[1] } else { '' }
# what esptool calls each chip. the classic ESP32 boards here are all D0WD (D0WD, D0WDQ6, D0WD-V3);
# a PICO or U4WDH chip needs its own pattern. the C5 and P4 lines are esptool's names, not run here
$want = @{ 'esp32' = '^ESP32-D0WD'; 'esp32c3' = '^ESP32-C3'; 'esp32s3' = '^ESP32-S3';
           'esp32c5' = '^ESP32-C5'; 'esp32p4' = '^ESP32-P4' }[$Chip]
$macOk = $seenMac -ne '' -and $seenMac -eq $Mac.ToLower()
$chipOk = $seenChip -ne '' -and $want -and $seenChip -match $want
if (-not ($macOk -and $chipOk)) { "NOT FLASHED: $Port has chip '$seenChip' MAC '$seenMac', meant $Chip $Mac"; exit 1 }
Push-Location $Dir
# --chip given now: esptool itself refuses a chip that is not this one
$f = & $Python -m esptool --chip $Chip -p $Port -b 460800 --before default_reset --after hard_reset write_flash "@flash_args" 2>&1
Pop-Location
# one "Hash of data verified" a part: bootloader, partition table, app
$ok = @($f | Select-String "Hash of data verified").Count
"flashed $Port ($seenChip $seenMac): $ok of 3 verified"
if ($ok -ne 3) { $f | Select-Object -Last 8; exit 1 }
