# read a board's whole flash into a file, only when the board is the one meant (its MAC). never
# overwrites a file that is there, and checks the size that came. prints no settings: the file holds
# the person's WiFi and home, so it stays outside every repository and every chat
#   backup-board.ps1 -Port <port> -Mac aa:bb:cc:dd:ee:ff -Out <backups>\board-before.bin
#   -Size 0x200000 for a 2 MB board, 0x1000000 for 16 MB; 4 MB by default
# keep backups in a folder no cleanup touches: a temp folder of the build PC was cleaned once and
# took working files with it
param([string]$Port, [string]$Mac, [string]$Out, [string]$Size = '0x400000',
      [string]$Python = "$env:USERPROFILE\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe")
if (Test-Path $Out) { "NOT READ: $Out is there already"; exit 1 }
$o = (& $Python -m esptool -p $Port chip_id 2>&1 | Out-String)
$seenMac = if ($o -match 'MAC:\s*([0-9a-f:]{17})') { $Matches[1] } else { '' }
if ($seenMac -eq '' -or $seenMac -ne $Mac.ToLower()) { "NOT READ: $Port has MAC '$seenMac', meant $Mac"; exit 1 }
# 460800: a CH340 fails reads at 921600
$f = & $Python -m esptool -p $Port -b 460800 read_flash 0 $Size $Out 2>&1
$want = [Convert]::ToInt64($Size, 16)
if (-not (Test-Path $Out) -or (Get-Item $Out).Length -ne $want) { "READ FAILED on $Port"; $f | Select-Object -Last 6; exit 1 }
"read $Port ($seenMac) into $(Split-Path -Leaf $Out): $((Get-Item $Out).Length) bytes"
