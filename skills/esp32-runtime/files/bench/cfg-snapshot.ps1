# cfg-snapshot.ps1: are a board's settings the same as before? Before and after a flash without erase, a
# restart, or a rig that should have put everything back.
#
# Run (COM5 is an example, give your board's port):
#   & .\cfg-snapshot.ps1 -Port COM5 -Out cfg-before.json                save the settings to a local file
#   & .\cfg-snapshot.ps1 -Port COM5 -Before cfg-before.json             compare with that file
#   & .\cfg-snapshot.ps1 -Port COM5 -Before cfg-before.json -Settle 20  for a board that has just started
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: nothing. The port opens with DTR and RTS low, so the board is not restarted.
# Prints: the version and how many fields came. With -Before, the NAMES of the fields that differ, never a
#   value. Left out of the compare, because they change by themselves: uptime, ran, reset, utc, newest
#   (empty after a flash till the board asks the site again), version, cc (home's country, asked from
#   OpenStreetMap at every start).
# The saved file holds the WiFi's name, the networks, the home and the watch list: keep it out of every
# repository and every chat.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$Out = '',
    [string]$Before = '',
    [int]$Settle = 0
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$now = $null
$sp = Open-Board $Port
try {
    if ($Settle -gt 0) {
        Watch-Board $sp $Settle @('crash')
    }
    $now = Get-BoardCfg $sp
} finally {
    $sp.Close()
}
if ($null -eq $now) {
    "${Port}: no OWLOS CFG came"
    exit 1
}
"${Port}: version $($now.version), $(@($now.PSObject.Properties).Count) fields"
if ($Out) {
    [System.IO.File]::WriteAllText($Out, $script:BenchCfgJson)
    "${Port}: saved to a local file"
}
if ($Before) {
    $was = [System.IO.File]::ReadAllText($Before) | ConvertFrom-Json
    $a = @($was.PSObject.Properties | ForEach-Object { $_.Name })
    $b = @($now.PSObject.Properties | ForEach-Object { $_.Name })
    $same = 0
    $differ = @()
    foreach ($n in $a) {
        if ($script:BenchMoving -contains $n -or $b -notcontains $n) {
            continue
        }
        if (($was.$n | ConvertTo-Json -Compress -Depth 4) -eq ($now.$n | ConvertTo-Json -Compress -Depth 4)) {
            $same++
        } else {
            $differ += $n
        }
    }
    $gone = @($a | Where-Object { $b -notcontains $_ })
    $new = @($b | Where-Object { $a -notcontains $_ })
    $d = 'none'
    if ($differ.Count) {
        $d = $differ -join ', '
    }
    $g = 'none'
    if ($gone.Count) {
        $g = $gone -join ', '
    }
    $w = 'none'
    if ($new.Count) {
        $w = $new -join ', '
    }
    "${Port}: $same fields the same; differ: $d; only before: $g; only now: $w"
}
