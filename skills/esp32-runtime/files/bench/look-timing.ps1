# look-timing.ps1: how fast does each look draw on this board, and where does a frame's time go?
#
# Run (COM5 is an example, give your board's port):
#   & .\look-timing.ps1 -Port COM5 -Looks 0,1,2,3
#   & .\look-timing.ps1 -Port COM5 -Looks 3 -Restart -Wait 45     the WEATHER look after a fresh start,
#                                                                   once the forecast has come
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: the look, -Seconds each. The look is saved from GET first and put back at the end,
#   also on Ctrl+C. No restart, unless -Restart: then the board is restarted at the open and left -Wait
#   seconds before the first look (the WiFi, the first map, the forecast).
# Prints, per look, from its 11th second on (the first 10 s hold the switch itself): the 10 s log's frame
#   rate and heap, ms a frame per layer (background, disc, compass, planes, panel, the rest, sending,
#   touch), the second core's lines, the internal RAM on PSRAM boards, any restart or crash.
# How to read it: "sending" near W*H*16 / SPI clock means the bus is the limit. A layer that grows after a
#   minute is state filling up.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int[]]$Looks = @(1, 2, 3),
    [int]$Seconds = 32,
    [switch]$Restart,
    [int]$Wait = 45
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$sp = Open-Board $Port -Restart:$Restart
if ($Restart) {
    "${Port}: restarted, waiting $Wait s"
    Watch-Board $sp $Wait @('weather', 'crash')
}
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
try {
    foreach ($look in $Looks) {
        "${Port}: look $look " + (Set-BoardField $sp 'look' $look)
        $t0 = Get-Date
        Watch-Board $sp 10 @('crash') $t0
        Watch-Board $sp ($Seconds - 10) @('frames', 'memory', 'crash') $t0
    }
} finally {
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"$Port done"
