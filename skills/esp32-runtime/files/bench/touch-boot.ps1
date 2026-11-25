# touch-boot.ps1: does the touch take a tap nobody made while the board starts? Restart it again and
# again, and see whether it comes up in the look it was in. A phantom tap on the look button moves it on.
#
# Run (COM5 and the MAC are examples, give your board's port and its MAC from your own notes):
#   & .\touch-boot.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Times 10
#   & .\touch-boot.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Chip esp32 -Times 5 -Seconds 25
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, esptool (IDF's Python, or -Python, or python
#   with pip's esptool on PATH), the board's MAC from your own list. Nobody touching the glass.
# Changes on the board: nothing kept. It is RESTARTED -Times times, each by esptool's chip_id after the
#   MAC (and the chip, with -Chip) matched; any doubt ends the rig. The settings are saved from GET first;
#   if a phantom tap moved the look, it is put back at the end, also on Ctrl+C.
# Prints: per restart, what the touch driver says at its start (which chip, whether the pen interrupt is
#   trusted), any line about a tap that names nothing of the person, the look after it (asked 3 s before
#   the end of each listen) and LOOK CHANGED when it is not the look before. At the end, how many restarts
#   changed the look: 0 is the pass.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Mac,
    [string]$Chip = '',
    [int]$Times = 10,
    [int]$Seconds = 25,
    [string]$Python = ''
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
$sp.Close()
if ($null -eq $before) {
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
"${Port}: look $($before.look) before"
$changed = 0
$done = 0
try {
    for ($n = 1; $n -le $Times; $n++) {
        if (-not (Test-BoardIdentity -Port $Port -Mac $Mac -Chip $Chip -Python $Python)) {
            "${Port}: NOT RESTARTED, stopped"
            break
        }
        $sp = Open-Board $Port
        $t0 = Get-Date
        "${Port}: restart $n"
        $asked = $false
        $look = $null
        try {
            while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
                $t = ((Get-Date) - $t0).TotalSeconds
                if (-not $asked -and $t -gt ($Seconds - 3)) {
                    Send-BoardLine $sp 'OWLOS GET'
                    $asked = $true
                }
                $l = Read-BoardLine $sp
                if ($null -eq $l) {
                    continue
                }
                if ($l -match 'OWLOS CFG \{' -and $l -match '"look":(\d+)') {
                    $look = $Matches[1]
                    $mark = ''
                    if ("$look" -ne "$($before.look)") {
                        $mark = '  LOOK CHANGED'
                    }
                    (Format-Seconds $t) + "  look $look$mark"
                    continue
                }
                $k = Get-KnownLine $l
                if ($k -and @('touch', 'crash') -contains $k.Topic) {
                    (Format-Seconds $t) + '  ' + $k.Text
                } elseif ($l -match '\btap|\bTAP|\bhit ' -and (Test-SafeLine $l)) {
                    (Format-Seconds $t) + '  ' + (Get-Printable $l)
                }
            }
        } finally {
            $sp.Close()
        }
        $done++
        if ($null -ne $look -and "$look" -ne "$($before.look)") {
            $changed++
        }
    }
} finally {
    $sp = Open-Board $Port
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"${Port}: $done restarts, the look changed after $changed"
