# epaper-keys.ps1: what does an e-paper board do when its keys are pressed, with no finger? Which keys it
# took, how the presses were gathered, every refresh of the glass, the map being made, errors and crashes.
#
# Run (COM5 is an example, give your board's port):
#   & .\epaper-keys.ps1 -Port COM5 -Keys "5:10,1:8"        MENU, 10 s, then UP, 8 s
#   & .\epaper-keys.ps1 -Port COM5 -Keys "1:1,1:1,1:6" -Out keys.txt
# The keys (core/ui.h, KEY_*): 1 up, 2 down, 3 ok, 4 ok held, 5 menu, 6 menu held, 7 back. Each step is
#   key:seconds to listen after it. The board takes OWLOS KEY n as if the key were pressed.
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, an e-paper build of OWLOS Flight Radar
#   (OWLOS KEY exists only in those builds; other boards ignore the line).
# Changes on the board: whatever the keys change (look, range, the menu's settings). Every setting is
#   saved from GET first and put back at the end, also on Ctrl+C; the weather's page is not kept by the
#   board and comes back to the dial at a restart. No restart.
# Prints: "key n: look, view, range" for a key taken at once, "key n gathered" and "keys taken after the
#   quiet: changed or as it was" for the gathered ones (they print together after 2 s of quiet), each
#   4-grey refresh and each drawing on top, the map's line, E and W lines that name nothing of the person,
#   any crash with its backtrace. -Out keeps the same lines in a file.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Keys,
    [string]$Out = ''
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('keys', 'epaper', 'map', 'warn', 'crash', 'console')
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
$w = $null
if ($Out) {
    $w = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
    $w.AutoFlush = $true
}
$t0 = Get-Date
try {
    Watch-Board $sp 1 $topics $t0 $null $w
    foreach ($step in $Keys.Split(',')) {
        $k, $wait = $step.Trim().Split(':')
        if ($k -notmatch '^[1-7]$') {
            "${Port}: '$step' is not a key from 1 to 7, skipped"
            continue
        }
        if (-not $wait) {
            $wait = 5
        }
        (Format-Seconds ((Get-Date) - $t0).TotalSeconds) + "  > KEY $k"
        Send-BoardLine $sp "OWLOS KEY $k"
        Watch-Board $sp ([double]$wait) $topics $t0 $null $w
    }
} finally {
    Restore-BoardCfg $sp $before
    if ($w) {
        $w.Close()
    }
    $sp.Close()
}
"$Port done"
