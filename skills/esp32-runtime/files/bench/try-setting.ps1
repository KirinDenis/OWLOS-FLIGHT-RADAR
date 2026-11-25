# try-setting.ps1: does the board take this value of one setting, draw with it, and not restart?
# For a new setting or a new value: the names' style on the map, the height's unit, a theme, a look.
#
# Run (COM5 is an example, give your board's port):
#   & .\try-setting.ps1 -Port COM5 -Field alt_unit -Value 1 -Listen 60      metres for a minute
#   & .\try-setting.ps1 -Port COM5 -Field names -Value 2 -Listen 30
#   & .\try-setting.ps1 -Port COM5 -Field look -Value 1 -Topics map,crash
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: the one field, for -Listen seconds. Every setting is saved from GET first and put
#   back at the end, also on Ctrl+C. The port opens with DTR and RTS low: no restart.
# Prints: the field before, the board's OK or ERR, the field after (a value out of range is skipped and
#   still answered OK: "after" shows whether it took), the lines of -Topics while it listens (by default the
#   console and any restart or crash), and what was put back.
# Only the numeric fields a SET can put back are taken (look, range, theme, names, alt_unit, dist_unit,
#   the units, backlight, night, the LED's and the radar's options and the rest in bench-serial.ps1).
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Field,
    [Parameter(Mandatory = $true)][double]$Value,
    [int]$Listen = 10,
    [string[]]$Topics = @('console', 'crash')
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
if ($script:BenchRestorable -notcontains $Field) {
    "REFUSED: $Field is not a numeric setting this rig can put back"
    exit 1
}
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
try {
    "${Port}: before " + (Get-CfgText $before @($Field))
    "${Port}: SET $Field $(Format-Value $Value): " + (Set-BoardField $sp $Field $Value)
    Watch-Board $sp $Listen $Topics
    $after = Get-BoardCfg $sp
    if ($null -eq $after) {
        "${Port}: no OWLOS CFG came after"
    } else {
        "${Port}: after " + (Get-CfgText $after @($Field))
    }
} finally {
    Restore-BoardCfg $sp $before
    $sp.Close()
}
