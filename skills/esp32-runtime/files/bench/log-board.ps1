# log-board.ps1: the board's whole serial log into a local file, for a long run: hours of it, read later
# for the trend of the heap and "at least", not only for crashes.
#
# Run (COM5 is an example, give your board's port):
#   & .\log-board.ps1 -Port COM5 -Out long-COM5.log -Minutes 120              the board goes on as it was
#   & .\log-board.ps1 -Port COM5 -Out long-COM5.log -Minutes 120 -Restart     from its boot
# Then read it through the filters only: bash log-summary.sh long-COM5.log
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it. Any firmware that logs at 115200.
# Changes on the board: nothing. The port opens with DTR and RTS low, so a board someone is using goes on.
#   -Restart: DTR and RTS up at the open, then down, which restarts the board, so the log has its boot.
#   Keep ONE long logger on a port: every new open may restart a board, and ends what it was doing.
# Writes: every line with the seconds since the start, the CFG line left out. The file still holds the
#   request URLs (the home), tile addresses and the network's name: keep it local, read it only through
#   log-summary.sh, mask anything before you share it.
# Prints: one line at the end. Ctrl+C ends it early with the file closed properly.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Minutes = 120,
    [switch]$Restart
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$sp = Open-Board $Port -Restart:$Restart
$t0 = Get-Date
$n = 0
$w = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
$w.AutoFlush = $true
try {
    while (((Get-Date) - $t0).TotalMinutes -lt $Minutes) {
        $l = Read-BoardLine $sp
        if ($null -eq $l -or $l.Contains('OWLOS CFG {')) {
            continue
        }
        $w.WriteLine((Format-Seconds ((Get-Date) - $t0).TotalSeconds) + ' ' + $l)
        $n++
    }
} finally {
    $w.Close()
    $sp.Close()
}
"logged $Port for $([math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) min, $n lines, into $(Split-Path -Leaf $Out)"
