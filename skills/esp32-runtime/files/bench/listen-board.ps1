# listen-board.ps1: what is a running board saying about one thing (its frames, its maps, its keys) right
# now, without touching it? Also the recorder for a person pressing a board's keys or touching its glass.
#
# Run (COM5 is an example, give your board's port):
#   & .\listen-board.ps1 -Port COM5                                     frames, memory, weather, crashes, 35 s
#   & .\listen-board.ps1 -Port COM5 -Topics map,crash -Seconds 60       the map's lines only
#   & .\listen-board.ps1 -Port COM5 -Topics keys,epaper,map,warn,crash -Seconds 240 -Out keys.txt
#                                                                       while a person presses the keys
#   & .\listen-board.ps1 -Port COM5 -Topics map,crash -Restart          from a restart
# Topics: console, frames, memory, map, weather, planes, net, epaper, keys, touch, light, crash, warn, all.
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: nothing. The port opens with DTR and RTS low, so a board someone is using goes on
#   as it was. -Restart restarts it once (RTS pulse) at the start.
# Prints: each known line of those topics with the seconds since the start, cut to what names nothing of
#   the person: never the CFG line, a web address, the network, the board's address or the home. -Out
#   keeps the same lines in a file, which is then safe to share.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Seconds = 35,
    [string[]]$Topics = @('frames', 'memory', 'weather', 'crash'),
    [string]$Out = '',
    [switch]$Restart
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$sp = Open-Board $Port
$w = $null
if ($Out) {
    $w = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
    $w.AutoFlush = $true
    $w.WriteLine("listening to $Port for $Seconds s: $($Topics -join ', ')")
}
try {
    if ($Restart) {
        Restart-Board $sp
        "${Port}: restarted, listening"
    } else {
        "${Port}: listening"
    }
    Watch-Board $sp $Seconds $Topics (Get-Date) $null $w
} finally {
    if ($w) {
        $w.WriteLine('done')
        $w.Close()
    }
    $sp.Close()
}
"$Port done"
