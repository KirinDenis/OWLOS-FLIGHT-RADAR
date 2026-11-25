# looks-stress.ps1: does a running board survive its looks switched under it every few seconds, the WEATHER
# look between each of the others? Each switch starts or stops the second core and takes or gives back
# the dial's tables, so a leak or a race shows here first.
#
# Run (COM5 is an example, give your board's port):
#   & .\looks-stress.ps1 -Port COM5
#   & .\looks-stress.ps1 -Port COM5 -Seconds 120 -Every 3
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: the look, every -Every seconds, in the order 3 0 3 1 3 2 3 0 1 2 3 0 3 (0 radar,
#   1 map, 2 classic, 3 weather). The look is saved from GET first and put back at the end, also on
#   Ctrl+C. No restart.
# Prints: each switch, the second core's start and stop with the heap, the frame rate and heap, and any
#   restart or crash. A board with one core (C3, C5) has no second core lines.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Seconds = 60,
    [int]$Every = 3
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$order = @(3, 0, 3, 1, 3, 2, 3, 0, 1, 2, 3, 0, 3)
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
$t0 = Get-Date
$k = 0
try {
    while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
        $look = $order[$k % $order.Count]
        Send-BoardLine $sp ('OWLOS SET {"look":' + $look + '}')
        (Format-Seconds ((Get-Date) - $t0).TotalSeconds) + "  > look $look"
        $k++
        Watch-Board $sp $Every @('frames', 'crash') $t0
    }
} finally {
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"$Port done, $k switches"
