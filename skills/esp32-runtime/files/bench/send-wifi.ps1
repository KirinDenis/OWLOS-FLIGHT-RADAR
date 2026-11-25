# send-wifi.ps1: does the board join this WiFi and make its first map, with nothing typed on its screen and
# no browser? It sends the network the way the site does: OWLOS SET {"ssid":..,"pass":..}.
#
# Run it YOURSELF, in your own PowerShell window. An agent never types, reads or sends a WiFi password.
#   & .\send-wifi.ps1 -Port COM5          (COM5 is an example, give your board's port)
# It asks for the network's name and its password here, at run time. The password is read hidden, never
#   printed, never written to a file or a log, and dropped as soon as the line is sent (a .NET string
#   cannot be wiped, so it is let go, not overwritten). The name shows as you type it and is not printed
#   again.
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board, a 2.4 GHz
#   network.
# Changes on the board: its WiFi. This is the one rig that leaves its change, because that is its job: the
#   network goes first in the five the board remembers, the others stay. To take it off again, use the
#   site's settings, or OWLOS SET {"forget":"<name>"}. The board's own log names the network (never the
#   password); this rig does not print that line.
# Prints, for -Listen seconds: the board's OK or ERR, WiFi up (no address, no name), WiFi lost with what its
#   reason means, the first maps, a new e-paper sheet, a restart or crash. Never the CFG line.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Listen = 60
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')

# what the board's JSON reader (console.c json_str) takes back: a backslash and a quote escaped
function ConvertTo-JsonText([string]$Text) {
    return ($Text -replace '\\', '\\' -replace '"', '\"')
}

$ssid = Read-Host 'WiFi name'
$secure = Read-Host 'WiFi password' -AsSecureString
if (-not $ssid) {
    'no name given, nothing sent'
    exit 1
}
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
$sp = Open-Board $Port
try {
    $pass = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    Send-BoardLine $sp ('OWLOS SET {"ssid":"' + (ConvertTo-JsonText $ssid) + '","pass":"' + (ConvertTo-JsonText $pass) + '"}')
    $pass = $null
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    $secure.Dispose()
}
$why = {
    param($l)
    if ($l -match 'WiFi lost \(reason (\d+)\)') {
        $r = [int]$Matches[1]
        if (@(15, 202, 204) -contains $r) {
            return "WiFi lost, reason ${r}: the password looks wrong"
        }
        if (@(201, 210, 211) -contains $r) {
            return "WiFi lost, reason ${r}: the network is not found (2.4 GHz? the name exact?)"
        }
        return "WiFi lost, reason $r"
    }
    if ($l -match '4-grey refresh, \d+ ms') {
        return "a new sheet printed: $($Matches[0])"
    }
    return $null
}
try {
    Watch-Board $sp $Listen @('console', 'net', 'map', 'crash') (Get-Date) $why
} finally {
    $sp.Close()
}
'done'
