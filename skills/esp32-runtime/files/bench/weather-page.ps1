# weather-page.ps1: does the board tell a listening page which weather page it shows, once per change,
# and turn the page when told? This is the twin's sync line, OWLOS WXPAGE n.
#
# Run (COM5 is an example, give your board's port):
#   & .\weather-page.ps1 -Port COM5
#   & .\weather-page.ps1 -Port COM5 -Settle 20      a board that has just started
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: the WEATHER look's page (wxpage), for a few seconds. The page is not kept by the
#   board (a restart brings the dial); the rig reads the page it was on first and turns back to it at the
#   end, also on Ctrl+C. No restart.
# The steps, with what the board should say:
#   SKY ON (a page listens) and a GET: OWLOS WXPAGE <the page it is on>
#   SET wxpage 2: OWLOS OK, then OWLOS WXPAGE 2
#   SET wxpage 2 again: OWLOS OK and nothing more, it is on that page already
#   GET: OWLOS WXPAGE 2 again, a GET tells it anew
#   SET wxpage 5, out of range: OWLOS OK and nothing more, the page stays
#   SET back to the page it was on, SKY OFF
# SKY ON makes the board send the planes it hears, with their positions, which give away the home's
# area. None of those lines is printed, nor the CFG line: only WXPAGE, OK or ERR, a restart or crash.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Settle = 0
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$t0 = Get-Date
$was = -1
# the first page the board names is the one it was on: kept to turn back to
$catch = {
    param($l)
    if ($script:was -lt 0 -and $l -match 'OWLOS WXPAGE (\d+)') {
        $script:was = [int]$Matches[1]
    }
    return $null
}

function Step([string]$Line, [string]$What) {
    "${Port}: $What"
    Send-BoardLine $sp $Line
    Watch-Board $sp 2 @('console', 'crash') $t0 $catch
}

$sp = Open-Board $Port
try {
    if ($Settle -gt 0) {
        "${Port}: settling"
        Watch-Board $sp $Settle @('crash') $t0
    }
    Step 'OWLOS SKY ON' 'SKY ON, a page listens: it names its page if it has not told one since a GET'
    Step 'OWLOS GET' 'GET: it names its page'
    if ($was -lt 0) {
        "${Port}: no OWLOS WXPAGE came, nothing changed"
        exit 1
    }
    Step 'OWLOS SET {"wxpage":2}' 'SET wxpage 2'
    Step 'OWLOS SET {"wxpage":2}' 'the same again: nothing new to say'
    Step 'OWLOS GET' 'GET: told again'
    Step 'OWLOS SET {"wxpage":5}' 'wxpage 5, out of range: skipped'
} finally {
    if ($was -ge 0) {
        Send-BoardLine $sp ('OWLOS SET {"wxpage":' + $was + '}')
        Watch-Board $sp 2 @('console', 'crash') $t0
        "${Port}: back to page $was"
    } else {
        "${Port}: the page it was on never came: the dial (0) is what a restart brings"
    }
    Send-BoardLine $sp 'OWLOS SKY OFF'
    Start-Sleep -Milliseconds 300
    $sp.Close()
}
"$Port done"
