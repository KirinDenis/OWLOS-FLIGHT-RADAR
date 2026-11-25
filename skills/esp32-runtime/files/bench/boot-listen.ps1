# boot-listen.ps1: after a restart, how long until the board says a given thing, and what does it come up
# in? By default: the weather's first answer (the forecast, the air, the day before's pressure).
#
# Run (COM5 and the MAC are examples, give your board's port and its MAC from your own notes):
#   & .\boot-listen.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff
#   & .\boot-listen.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Chip esp32c3 -Until 'map: zoom' -Seconds 60
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, esptool (IDF's Python, or -Python, or python
#   with pip's esptool on PATH). The board's MAC: from your own list of boards, kept outside every
#   repository, or from OWLOS HELLO's answer.
# Changes on the board: nothing kept. It is RESTARTED once: esptool's chip_id resets it on the way in and
#   out, and does so only after the MAC (and the chip, with -Chip) matched. Any doubt is a refusal.
# Prints: the known lines of the weather, the net, the console and any crash, with the seconds since the
#   restart; the look it came up in (asked at 15 s, that field alone); "seen after N s" when a line
#   matching -Until came, or that it did not within -Seconds. The matched line itself is printed only
#   when it names nothing of the person.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Mac,
    [string]$Chip = '',
    [string]$Until = 'weather: \d+ bytes',
    [int]$Seconds = 100,
    [string]$Python = ''
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
if (-not (Test-BoardIdentity -Port $Port -Mac $Mac -Chip $Chip -Python $Python)) {
    "${Port}: NOT RESTARTED"
    exit 1
}
$sp = Open-Board $Port
$t0 = Get-Date
"${Port}: restarted, listening"
$asked = $false
$seen = -1.0
try {
    while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
        $t = ((Get-Date) - $t0).TotalSeconds
        if (-not $asked -and $t -gt 15) {
            Send-BoardLine $sp 'OWLOS GET'
            $asked = $true
        }
        $l = Read-BoardLine $sp
        if ($null -eq $l) {
            continue
        }
        if ($l -match 'OWLOS CFG \{' -and $l -match '"look":(\d+)') {
            (Format-Seconds $t) + "  look $($Matches[1])"
            continue
        }
        if ($l -match $Until) {
            $seen = $t
            $k = Get-KnownLine $l
            if ($k) {
                (Format-Seconds $t) + '  ' + $k.Text
            } elseif (Test-SafeLine $l) {
                (Format-Seconds $t) + '  ' + (Get-Printable $l)
            }
            break
        }
        $k = Get-KnownLine $l
        if ($k -and @('weather', 'net', 'console', 'crash') -contains $k.Topic) {
            (Format-Seconds $t) + '  ' + $k.Text
        }
    }
} finally {
    $sp.Close()
}
if ($seen -ge 0) {
    "${Port}: '$Until' seen after $([math]::Round($seen, 1)) s"
} else {
    "${Port}: '$Until' not seen in $Seconds s"
}
