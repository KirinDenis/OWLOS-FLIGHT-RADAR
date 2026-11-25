# weather-units.ps1: does the board take the weather's units over the console, refuse a unit that does
# not exist, and keep the others?
#
# Run (COM5 is an example, give your board's port):
#   & .\weather-units.ps1 -Port COM5
#   & .\weather-units.ps1 -Port COM5 -Settle 25      a board that has just started
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: temp_unit, pressure_unit, wind_unit, rain_unit, a few seconds each. All four are
#   saved from GET first and put back at the end as they were found, also on Ctrl+C. No restart.
# The steps: F, mmHg, m/s and inches (2, 2, 2, 2); then inHg and knots (pressure 3, wind 4); then
#   temp_unit 7, which does not exist: the board skips it and still answers OWLOS OK, so GET must show
#   temp_unit unchanged. Each unit 0 is AUTO, the home country's own.
# Prints: after each step the four units, and whether the home's country is known yet (the letters
#   themselves are not printed, they say where the home is), OK or ERR, any restart or crash.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Settle = 0
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$units = @('temp_unit', 'pressure_unit', 'wind_unit', 'rain_unit')

function Show-Units {
    $c = Get-BoardCfg $sp
    if ($null -eq $c) {
        '  no CFG came'
        return
    }
    $cc = 'not known yet'
    if ($c.PSObject.Properties['cc'] -and "$($c.cc)" -ne '') {
        $cc = 'known'
    }
    '  ' + (Get-CfgText $c $units) + ", home's country $cc"
}

function Send-Units([string]$Json, [string]$What) {
    "${Port}: $What"
    Send-BoardLine $sp "OWLOS SET $Json"
    $a = Wait-BoardAnswer $sp
    if (-not $a) {
        $a = 'no answer'
    }
    "  $a"
    Show-Units
}

$sp = Open-Board $Port
if ($Settle -gt 0) {
    "${Port}: settling"
    Watch-Board $sp $Settle @('crash')
}
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
try {
    "${Port}: as found"
    '  ' + (Get-CfgText $before $units)
    Send-Units '{"temp_unit":2,"pressure_unit":2,"wind_unit":2,"rain_unit":2}' 'F, mmHg, m/s, inches'
    Send-Units '{"pressure_unit":3,"wind_unit":4}' 'inHg, knots'
    Send-Units '{"temp_unit":7}' 'temp_unit 7, out of range: skipped, temp_unit must stay 2'
    Watch-Board $sp 1 @('crash')
} finally {
    "${Port}: back as found"
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"$Port done"
