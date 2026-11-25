# get-repeat.ps1: does the board answer GET whole, again and again, without a restart? The CFG line grows
# with every new setting, and the console task that prints it has a small stack.
#
# Run (COM5 is an example, give your board's port):
#   & .\get-repeat.ps1 -Port COM5
#   & .\get-repeat.ps1 -Port COM5 -Times 10 -Gap 2
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, OWLOS Flight Radar on the board.
# Changes on the board: nothing kept. Opening the port RESTARTS it (DTR and RTS up, then down); the rig
#   then waits 35 s for the WiFi and the first fetches, so GET meets a busy board.
# Prints, for each GET: whether the CFG line came whole (it ends with "}") or CUT, its length, and only
#   its version and reset fields; crash text found inside the line (a panic in the middle of it); the
#   site's version lines; every crash line of the log. Never the line itself.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Times = 5,
    [double]$Gap = 3
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$show = {
    param($l)
    $at = $l.IndexOf('OWLOS CFG ')
    if ($at -ge 0) {
        $c = $l.Substring($at)
        $v = '?'
        if ($c -match '"version":"([0-9a-z.]+)"') {
            $v = $Matches[1]
        }
        $r = '?'
        if ($c -match '"reset":"([a-z ]+)"') {
            $r = $Matches[1]
        }
        $whole = 'CUT'
        if ($c.TrimEnd().EndsWith('}')) {
            $whole = 'whole'
        }
        $said = "CFG ($whole, $($c.Length) chars): version $v, reset $r"
        $g = $c.IndexOf('Guru')
        if ($g -ge 0) {
            $said += '; in it: ' + (Get-Printable $c.Substring($g))
        }
        return $said
    }
    if ($l -match "the site's version: HTTP -?\d+, [0-9a-z. ]+, this one [0-9a-z. ]+|the site has a newer version: [0-9a-z. ]+") {
        return $Matches[0]
    }
    return $null
}
$sp = Open-Board $Port -Restart
$t0 = Get-Date
"${Port}: restarted"
try {
    Watch-Board $sp 35 @('crash') $t0 $show
    for ($i = 1; $i -le $Times; $i++) {
        (Format-Seconds ((Get-Date) - $t0).TotalSeconds) + "  > OWLOS GET ($i)"
        Send-BoardLine $sp 'OWLOS GET'
        Watch-Board $sp $Gap @('crash') $t0 $show
    }
} finally {
    $sp.Close()
}
"$Port done"
