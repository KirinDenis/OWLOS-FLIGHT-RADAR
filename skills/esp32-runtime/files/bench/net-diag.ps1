# net-diag.ps1: what does the board's network do after a start, and when? Each request's server, status,
# bytes and time, the WiFi's ups and downs, TLS and HTTP failures, cut to their numbers.
#
# Run (COM5 is an example, give your board's port):
#   & .\net-diag.ps1 -Port COM5
#   & .\net-diag.ps1 -Port COM5 -Seconds 300
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: nothing kept. Opening the port RESTARTS it (DTR and RTS up, then down), so the
#   listen starts at its boot.
# Prints, with the time from the board's own log stamp (seconds since its boot): the planes' requests
#   (server, HTTP code, bytes, ms), the site's version check, the map of boards' count, the weather's
#   line, a watched plane's lookup (its code only), the frame rate and heap, WiFi started (how many
#   networks kept), looked round, WiFi up, WiFi lost with its reason, on to the next network, asking for the
#   time, esp-tls and HTTP client failures with their code, a crash. No URL, no network name, no address,
#   no home, no watched plane: every line is matched to a known shape and cut to its numbers.
# Reasons for WiFi lost, as the site reads them: 15, 202, 204 the password looks wrong; 201, 210, 211 the
#   network is not found (is it 2.4 GHz, is the name exact?).
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Seconds = 150
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$show = {
    param($l)
    $at = '      ? s'
    if ($l -match '^[IWE] \((\d+)\)') {
        $at = Format-Seconds ([double]$Matches[1] / 1000.0)
    }
    if ($l -match 'watch [^:"]*: [A-Z0-9.]+ by \w+, HTTP (-?\d+)') {
        return "$at  watch lookup HTTP $($Matches[1])"
    }
    if ($l -match 'watch [^:"]*: [A-Z0-9.]+ by \w+, ') {
        return "$at  watch lookup answered"
    }
    if ($l -match '(esp-tls|HTTP_CLIENT|transport_base).*?(error|fail|Failed)[^\d]*(-?0x[0-9a-fA-F]+|-?\d+)?') {
        return "$at  tls/http: $($Matches[1]) $($Matches[2]) $($Matches[3])"
    }
    $k = Get-KnownLine $l
    if ($k -and @('planes', 'net', 'weather', 'crash') -contains $k.Topic) {
        return "$at  $($k.Text)"
    }
    if ($k -and $k.Topic -eq 'frames' -and $k.Text -match 'fps') {
        return "$at  $($k.Text)"
    }
    return $null
}
$sp = Open-Board $Port -Restart
"${Port}: restarted, listening $Seconds s"
$end = (Get-Date).AddSeconds($Seconds)
try {
    while ((Get-Date) -lt $end) {
        $l = Read-BoardLine $sp
        if ($null -eq $l) {
            continue
        }
        $said = & $show $l
        if ($said) {
            $said
        }
    }
} finally {
    $sp.Close()
}
"$Port done"
