# screen-light.ps1: does the screen's light turn down over the console, refuse a level that does not exist,
# stay down through a restart, and does the night's light follow the sun at home?
#
# Run (COM5 is an example, give your board's port):
#   & .\screen-light.ps1 -Port COM5
#   & .\screen-light.ps1 -Port COM5 -Step 3 -NightWait 60
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on a board whose light
#   turns down (its CFG has a backlight field: not the e-paper boards, the round board, the RPi LCD or the
#   CrowPanel Advance 5"). A home and the time, for the night part.
# Changes on the board: backlight (0 100%, 1 50%, 2 25%, 3 10%) and night (0 off, else a step). Both are
#   saved from GET first and put back at the end, also on Ctrl+C. The board is RESTARTED once in the
#   middle (RTS pulse), to see the level kept in flash.
# The steps: backlight -Step; backlight 7, which does not exist (skipped, still OK: the level must stay);
#   a restart; night -Step, then -NightWait seconds for the sun's word and the light.
# Prints: the light's own lines ("the screen's light: 25%", "the sun at home: down, ..."), the backlight
#   and night fields and whether the board knows the time, OK or ERR, a restart or crash.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Step = 2,
    [int]$NightWait = 40
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('light', 'console', 'crash')

function Show-Light {
    $c = Get-BoardCfg $sp
    if ($null -eq $c) {
        '  no CFG came'
        return
    }
    $t = 'not known yet'
    if ($c.PSObject.Properties['utc'] -and [int64]$c.utc -gt 0) {
        $t = 'known'
    }
    '  ' + (Get-CfgText $c @('backlight', 'night')) + ", the time $t"
}

function Step([string]$Line, [string]$What, [double]$Then) {
    "${Port}: $What"
    Send-BoardLine $sp $Line
    Watch-Board $sp $Then $topics
    Show-Light
}

$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
if (-not $before.PSObject.Properties['backlight']) {
    $sp.Close()
    "${Port}: no backlight field: this board's light does not turn down, nothing to test"
    exit 0
}
try {
    "${Port}: as found"
    '  ' + (Get-CfgText $before @('backlight', 'night'))
    Step ('OWLOS SET {"backlight":' + $Step + '}') "backlight $Step" 4
    Step 'OWLOS SET {"backlight":7}' 'backlight 7, out of range: skipped, the level must stay' 3
    "${Port}: restarted"
    Restart-Board $sp
    Watch-Board $sp 25 $topics
    Show-Light
    Step ('OWLOS SET {"night":' + $Step + '}') "night $Step, the light follows the sun at home" $NightWait
} finally {
    "${Port}: back as found"
    Restore-BoardCfg $sp $before
    Watch-Board $sp 3 $topics
    $sp.Close()
}
"$Port done"
