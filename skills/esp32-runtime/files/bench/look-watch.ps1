# look-watch.ps1: what happens when a board is put in this look? Its refreshes, the map being made, every
# error and warning, and a crash with its whole backtrace, so the backtrace can go to addr2line.
#
# Run (COM5 is an example, give your board's port):
#   & .\look-watch.ps1 -Port COM5 -Look 1 -Listen 30
#   & .\look-watch.ps1 -Port COM5 -Look 3 -Listen 60 -Out look3.txt
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board. For the
#   backtrace: the ELF of the very build on the board (check the "ELF file SHA256" line).
# Changes on the board: the look, for -Listen seconds. It is saved from GET first and put back at the end,
#   also on Ctrl+C. No restart.
# Prints: the look before and after, OK or ERR, the e-paper's refreshes (a full 4-grey one with how many
#   greys were wrong, or drawn on top with the grey debt), the map's line (tiles whole of tiles asked, the
#   time, the heap), the keys, E and W lines that name nothing of the person, and every crash line (Guru
#   Meditation, Backtrace, PC, EXCVADDR, EXCCAUSE, rst:, task_wdt, ELF file SHA256). -Out keeps the same
#   lines in a file.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][int]$Look,
    [int]$Listen = 30,
    [string]$Out = ''
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('console', 'crash', 'warn', 'epaper', 'map', 'keys')
$sp = Open-Board $Port
$before = Get-BoardCfg $sp
if ($null -eq $before) {
    $sp.Close()
    "${Port}: no OWLOS CFG came, nothing changed"
    exit 1
}
$w = $null
if ($Out) {
    $w = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
    $w.AutoFlush = $true
}
try {
    "${Port}: before " + (Get-CfgText $before @('look'))
    $t0 = Get-Date
    Send-BoardLine $sp ('OWLOS SET {"look":' + $Look + '}')
    Watch-Board $sp $Listen $topics $t0 $null $w
    $after = Get-BoardCfg $sp
    if ($null -ne $after) {
        "${Port}: after " + (Get-CfgText $after @('look'))
    }
} finally {
    Restore-BoardCfg $sp $before
    if ($w) {
        $w.Close()
    }
    $sp.Close()
}
"$Port done"
