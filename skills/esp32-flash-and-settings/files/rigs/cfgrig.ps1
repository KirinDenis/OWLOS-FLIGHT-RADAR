# cfgrig: the site's way over a serial port, from PowerShell, so an agent can test settings with no
# person and no browser. opens with DTR and RTS up, drops both, HELLO till READY, then the lines
# given. the board's CFG is kept in a file, never printed: only which fields differ from a saved
# one, and the values that say nothing about the person. from the log only the lines that matter;
# the raw log goes to -Raw.
#   & .\cfgrig.ps1 -Port <port> -Send @('OWLOS GET') -SaveCfg before.json -Seconds 8
#   & .\cfgrig.ps1 -Port <port> -Send @('OWLOS SET {"look":1}', 'WAIT 2', 'OWLOS GET') -CompareCfg before.json
#   -Mac aa:bb:cc:dd:ee:ff: nothing is sent unless READY names that MAC (firmware 0.8 on says
#   "OWLOS READY flight-radar <version> <build> <mac>", the MAC as 12 hex digits)
# run it IN-PROCESS as above: through `powershell -File` the JSON loses its quotes.
# made for OWLOS Flight Radar: the field names in the CFG line below and the log patterns are its own
param([string]$Port, [string[]]$Send = @(), [int]$Seconds = 20, [string]$SaveCfg = "", [string]$CompareCfg = "",
      [string]$Raw = "", [string[]]$Mask = @(), [string]$Mac = "")   # -Mask: texts never printed
$meant = ($Mac -replace '[^0-9a-fA-F]', '').ToLower()
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 100
$sp.NewLine = "`n"
$sp.Open()
$t0 = Get-Date
function T { "{0,6:N2}" -f ((Get-Date) - $t0).TotalSeconds }
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$ready = $false
$hello = 0
$nextHello = 0
$queue = [System.Collections.Generic.List[string]]::new()
foreach ($s in $Send) { $queue.Add($s) }
$cfg = $null
$holdUntil = 0
$rawLines = [System.Collections.Generic.List[string]]::new()
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $now = ((Get-Date) - $t0).TotalSeconds
    if (-not $ready -and $now -ge $nextHello -and $hello -lt 14) { $sp.Write("`nOWLOS HELLO`n"); $hello++; $nextHello = $now + 0.8 }
    if ($ready -and $queue.Count -and $now -ge $holdUntil -and $queue[0] -match '^WAIT (\d+)') {   # WAIT n: the next line n s later
        $holdUntil = $now + [int]$Matches[1]; $queue.RemoveAt(0)
    }
    elseif ($ready -and $queue.Count -and $now -ge $holdUntil) {
        $l = $queue[0]; $queue.RemoveAt(0); $sp.Write("`n$l`n")
        # what is typed into a setting is masked in what the rig prints
        $e = $l -replace '"(ssid|pass|key|lat|lon|place|country)":("[^"]*"|[-0-9.]+)', '"$1":<masked>'
        foreach ($s in $Mask) { if ($s) { $e = $e -replace [regex]::Escape($s), '<masked>' } }
        "$(T) >> $e"
        Start-Sleep -Milliseconds 200
    }
    try {
        $l = $sp.ReadLine().TrimEnd("`r")
        $rawLines.Add($l)
        if ($l -match '^OWLOS READY' -and $meant -and ($l.Trim() -split ' ')[-1].ToLower() -ne $meant) {
            "$(T) READY from another board than the one meant: nothing sent"   # its MAC is not printed
            break
        }
        if ($l -match '^OWLOS READY') { $ready = $true; "$(T) READY" }
        elseif ($l -match 'OWLOS CFG (\{.*\})') {
            $cfg = $Matches[1] | ConvertFrom-Json
            "$(T) CFG: look $($cfg.look), map $($cfg.map), ground $($cfg.ground), theme $($cfg.theme), up $($cfg.up), flip $($cfg.flip), src $($cfg.src), newest [$($cfg.newest)], uptime $($cfg.uptime), reset $($cfg.reset), version $($cfg.version)"
        }
        elseif ($l -match '^OWLOS (OK|ERR)') { "$(T) $l" }
        elseif ($l -match "site's version|newer version") { "$(T) " + ($l -replace '^.*?\) ', '') }
        elseif ($l -match 'rst:|Guru|panic|abort|WDT|settings') { "$(T) " + ($l -replace '".*?"', '"<masked>"' -replace '-?\d+\.\d{3,}', '<n>') }
    } catch [System.TimeoutException] { }
}
$sp.Close()
if ($Raw) { $rawLines | Set-Content -Encoding utf8 $Raw }
if ($cfg -and $SaveCfg) { $cfg | ConvertTo-Json -Compress | Set-Content -Encoding utf8 $SaveCfg; "CFG saved" }
if ($cfg -and $CompareCfg) {
    $old = Get-Content -Raw $CompareCfg | ConvertFrom-Json
    # what changes by itself: the run's times, the version flashed, the site's version (empty till the
    # board asks again), the clock, and home's country (asked from OpenStreetMap at start, and empty for
    # minutes when it refuses after several quick restarts)
    $skip = @('uptime', 'ran', 'reset', 'version', 'newest', 'utc', 'cc')
    $diff = @()
    foreach ($p in $old.PSObject.Properties) {
        if ($skip -contains $p.Name) { continue }
        if ("$($cfg.($p.Name))" -ne "$($p.Value)") { $diff += $p.Name }
    }
    if ($diff.Count) { "DIFFERS from the saved CFG: $($diff -join ', ')" } else { "same as the saved CFG in every field ($($skip -join ', ') left out)" }
}
if (-not $cfg -and ($SaveCfg -or $CompareCfg)) { "NO CFG came" }
