# cycle-looks.ps1: does the map come back whole, and does the board stay up, when the looks and the range
# change every few seconds the way the site sends them, and then when it is left alone?
#
# Run (COM5 is an example, give your board's port):
#   & .\cycle-looks.ps1 -Port COM5 -Out cycle-COM5.log
#   & .\cycle-looks.ps1 -Port COM5 -Out cycle-COM5.log -Minutes 30 -Looks 0,1,2 -Settle 5
# Then read the log through the filters only: bash log-summary.sh cycle-COM5.log
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board, its WiFi.
# Changes on the board: look and range, every -Every seconds for -Minutes. Nothing is changed before the
#   board's own CFG line came (asked at 38 s); look and range are put back from it at the end, also on
#   Ctrl+C. The rig used to leave every board at 5 km before it did that. Opening the port RESTARTS the
#   board (DTR and RTS up, then down), so the log starts at its boot.
# The cycle: look after look of -Looks (0 radar, 1 map, 2 classic, 3 weather), the range from a fixed
#   list of 12 (3 to 250, in the board's distance unit) changed every round, from 45 s after the start,
#   then -Settle minutes left alone, to see whether the map comes back.
# Writes: every line of the board into -Out with the seconds since the start, each command marked RIG,
#   the CFG line left out. The raw file still holds the request URLs (the home) and the network's name:
#   keep it local and read it only with log-summary.sh.
# Prints: one line at the end, how many commands and whether look and range were put back.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Minutes = 20,
    [int]$Every = 5,
    [int]$Settle = 3,
    [int[]]$Looks = @(0, 1, 2, 3)
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$ranges = @(25, 12, 60, 5, 120, 30, 8, 250, 18, 45, 3, 90)
$sp = Open-Board $Port -Restart
$t0 = Get-Date
$w = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
$w.AutoFlush = $true
$next = 45.0                                   # the WiFi and the first map before the first command
$k = 0
$asked = $false
$own = $null                                   # the board's settings as it came up, from its CFG line
try {
    while ($true) {
        $t = ((Get-Date) - $t0).TotalSeconds
        if ($t -ge ($Minutes + $Settle) * 60) {
            break
        }
        if (-not $asked -and $t -ge 38) {
            Send-BoardLine $sp 'OWLOS GET'
            $asked = $true
        }
        if ($t -ge $next -and $t -lt $Minutes * 60) {
            if ($null -eq $own) {
                # no CFG line yet: nothing is changed before it came. asked again every 10 s
                Send-BoardLine $sp 'OWLOS GET'
                $next = $t + 10
            } else {
                $look = $Looks[$k % $Looks.Count]
                $range = $ranges[[math]::Floor($k / $Looks.Count) % $ranges.Count]
                Send-BoardLine $sp ('OWLOS SET {"look":' + $look + ',"range":' + $range + '}')
                $w.WriteLine((Format-Seconds $t) + " RIG look $look range $range")
                $k++
                $next = $t + $Every
            }
        }
        $l = Read-BoardLine $sp
        if ($null -eq $l) {
            continue
        }
        $at = $l.IndexOf('OWLOS CFG {')
        if ($at -ge 0) {
            if ($null -eq $own) {
                try {
                    $own = $l.Substring($at + 10) | ConvertFrom-Json
                    $w.WriteLine((Format-Seconds $t) + " RIG the board's own look $($own.look) range $(Format-Value $own.range)")
                } catch {
                    $own = $null
                }
            }
            continue                           # the line has the WiFi and the home: never in the log
        }
        $w.WriteLine((Format-Seconds ((Get-Date) - $t0).TotalSeconds) + ' ' + $l)
    }
} finally {
    if ($null -ne $own) {
        Restore-BoardCfg $sp $own
        $w.WriteLine("RIG put back: look $($own.look) range $(Format-Value $own.range)")
    }
    $w.Close()
    $sp.Close()
}
$back = 'NOT PUT BACK: no CFG line came, nothing was changed'
if ($null -ne $own) {
    $back = "look $($own.look) and range $(Format-Value $own.range) put back"
}
"cycled $Port for $Minutes min, $k commands, then $Settle min alone, into $(Split-Path -Leaf $Out); $back"
