# weather-pages.ps1: how fast does the WEATHER look draw each of its pages (the dial, the pressure, the
# temperature), and with -WithBare, with the radar's panel and without it? Does any page crash or eat the
# heap?
#
# Run (COM5 is an example, give your board's port):
#   & .\weather-pages.ps1 -Port COM5
#   & .\weather-pages.ps1 -Port COM5 -WithBare -Each 20
#   & .\weather-pages.ps1 -Port COM5 -Settle 30      a board that has just been flashed
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: the look (to WEATHER, 3), the weather's page, and with -WithBare the panel (bare 0
#   and 1). The look and bare are saved from GET first, the page through a listening page (SKY ON, its
#   planes never printed); all are put back at the end, also on Ctrl+C. No restart.
# Prints, for each page: the frame rate and heap, where a frame's time goes, the second core's lines, the
#   weather's own line (its numbers are the home's weather, not its place), OK or ERR, a restart or crash.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Settle = 0,
    [int]$Each = 22,
    [switch]$WithBare
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('frames', 'memory', 'weather', 'console', 'crash')
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
$page = Get-BoardWxPage $sp
try {
    "${Port}: as found " + (Get-CfgText $before @('look', 'bare')) + ", page $page"
    if ("$($before.look)" -ne '3') {
        "${Port}: look 3, WEATHER: " + (Set-BoardField $sp 'look' 3)
        Watch-Board $sp 10 @('crash')
    }
    $bares = @($null)
    if ($WithBare) {
        $bares = @(0, 1)
    }
    foreach ($b in $bares) {
        if ($null -ne $b) {
            "${Port}: bare $b " + (Set-BoardField $sp 'bare' $b)
        }
        foreach ($pg in 1, 2, 0) {
            "${Port}: page $pg"
            Send-BoardLine $sp ('OWLOS SET {"wxpage":' + $pg + '}')
            Watch-Board $sp $Each $topics
        }
    }
} finally {
    if ($page -ge 0) {
        Send-BoardLine $sp ('OWLOS SET {"wxpage":' + $page + '}')
        Watch-Board $sp 1 @('crash')
    }
    Restore-BoardCfg $sp $before
    $sp.Close()
}
"$Port done"
