# get-fields.ps1: what does the board say for these settings, and nothing else?
#
# Run (COM5 is an example, give your board's port):
#   & .\get-fields.ps1 -Port COM5 -Names look,range,uptime,reset
#   & .\get-fields.ps1 -Port COM5 -Names led,look,alerts -LedTest     run the LED's test once, then GET
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: nothing that is kept. -LedTest sends {"led_test":1}, which runs the LED's test once
#   and saves nothing. The port opens with DTR and RTS low, so the board is not restarted.
# Prints: name=value for each field named, "(none)" for a field this build does not have. The private
#   fields (ssid, nets, lat, lon, place, country, cc, key, watch) are refused: "(private, not shown)".
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string[]]$Names,
    [switch]$LedTest
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$sp = Open-Board $Port
try {
    if ($LedTest) {
        "${Port}: the LED's test: " + (Set-BoardField $sp 'led_test' 1)
    }
    $c = Get-BoardCfg $sp
    if ($null -eq $c) {
        "${Port}: no OWLOS CFG came"
    } else {
        "${Port}: " + (Get-CfgText $c $Names)
    }
} finally {
    $sp.Close()
}
