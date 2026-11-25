# map-range.ps1: at this range, does the map come whole, how long does it take, and how much heap is left
# while the tiles are read? Then back to the board's own range: does that map come back too?
#
# Run (COM5 is an example, give your board's port):
#   & .\map-range.ps1 -Port COM5 -Range 120
#   & .\map-range.ps1 -Port COM5 -Range 3 -Seconds 60
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board, its WiFi, a
#   look with a map (MAP or CLASSIC) for the map lines to come.
# Changes on the board: the range (2 to 250, in the board's distance unit), for -Seconds. It is saved from
#   GET first and put back at the end, also on Ctrl+C; then 20 s more of the map's lines. No restart.
# Prints: the map's lines only (zoom, tiles whole of tiles asked, the time, the heap "at least" after the
#   tiles came and after reading them), map memory warnings, a restart or crash. Nothing of the network.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][double]$Range,
    [int]$Seconds = 40
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('map', 'crash')
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
$t0 = Get-Date
try {
    "${Port}: " + (Get-CfgText $before @('look', 'range'))
    "${Port}: range $(Format-Value $Range) " + (Set-BoardField $sp 'range' $Range)
    Watch-Board $sp $Seconds $topics $t0
} finally {
    "${Port}: range back to $(Format-Value $before.range)"
    Restore-BoardCfg $sp $before
    Watch-Board $sp 20 $topics $t0
    $sp.Close()
}
"$Port done"
