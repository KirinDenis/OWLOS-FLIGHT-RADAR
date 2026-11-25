# soak-board.ps1: the soak before every beta. Does the board stay up, keep its heap and make whole maps for
# an hour: first under the looks and ranges changed every 5 s, then left alone in one look?
#
# Run, one PowerShell window per board, all boards of the bench at once (COM5 is an example):
#   & .\soak-board.ps1 -Port COM5 -LogDir soak
#   & .\soak-board.ps1 -Port COM5 -LogDir soak -Cycle 15 -Idle 15 -IdleLook 1
# Then read both logs through the filters only:
#   bash log-summary.sh soak/soak-COM5-cycle.log
#   bash log-summary.sh soak/soak-COM5-idle.log
# Needs: Windows PowerShell 5.1, bench-serial.ps1, cycle-looks.ps1 and log-board.ps1 beside it, OWLOS
#   Flight Radar on the board, its WiFi.
# Changes on the board: look and range during the cycle (cycle-looks.ps1 puts them back), then the look
#   -IdleLook for the idle part. Every setting is saved from GET first and put back at the end, also on
#   Ctrl+C. The board is RESTARTED at the start of each part (the port's open), so each log has its boot.
# Writes: LogDir\soak-<port>-cycle.log and LogDir\soak-<port>-idle.log, the board's raw lines (no CFG line)
#   with the seconds. They hold the request URLs (the home) and the network's name: keep them local.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$LogDir,
    [int]$Cycle = 30,
    [int]$Idle = 35,
    [int]$IdleLook = 0,
    [int[]]$Looks = @(0, 1, 2, 3)
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir | Out-Null
}
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
$sp.Close()
if ($null -eq $before) {
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
$name = $Port -replace '[^A-Za-z0-9]', ''
$cycleLog = Join-Path $LogDir "soak-$name-cycle.log"
$idleLog = Join-Path $LogDir "soak-$name-idle.log"
try {
    & (Join-Path $PSScriptRoot 'cycle-looks.ps1') -Port $Port -Out $cycleLog -Minutes $Cycle -Every 5 -Settle 3 -Looks $Looks
    $sp = Open-Board $Port
    try {
        "${Port}: idle in look $IdleLook " + (Set-BoardField $sp 'look' $IdleLook)
    } finally {
        $sp.Close()
    }
    & (Join-Path $PSScriptRoot 'log-board.ps1') -Port $Port -Out $idleLog -Minutes $Idle -Restart
} finally {
    $sp = Open-Board $Port
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"${Port}: soak done"
