# bench-serial.ps1: the shared part of the bench rigs in this folder. Not a rig: it opens nothing by itself.
# Every rig loads it with:
#   . (Join-Path $PSScriptRoot 'bench-serial.ps1')
#
# Made for OWLOS Flight Radar's console (firmware/main/console.c, on_line() in firmware/main/main.c): text
# lines at 115200 baud each way, with the board's log on the same line. "OWLOS GET" is answered by one
# "OWLOS CFG {json}" line, "OWLOS SET {json}" by "OWLOS OK" or "OWLOS ERR <why>". A SET value out of range
# is skipped and the board still says OK: read the field back with GET to know.
#
# The rules every rig keeps through these functions:
#   - The CFG line is never printed. It holds the WiFi's name, the remembered networks, the home, the
#     home's country and the watch list. A rig prints only the fields it names, never the private ones.
#   - A log line is printed only when it matches a known shape (Get-KnownLine), or passes Test-SafeLine:
#     no web address, no quoted name, no network, no home, no IP address, no long decimal.
#   - What a rig changes is saved with Get-BoardCfg first; Restore-BoardCfg puts back every field a SET
#     can put back. Rigs call it in a finally block, so Ctrl+C puts it back too.
#   - A port is never assumed: -Port is required. Rigs that restart a board through esptool check its MAC
#     (and its chip, when given) first, and print neither MAC.
#
# Needs: Windows PowerShell 5.1 or later (System.IO.Ports). esptool only for Test-BoardIdentity.
# Library lines (a refusal, a crash seen while waiting, what was put back) go to the host with Write-Host,
# so they show even while Ctrl+C stops a rig. To keep everything in a file, run a rig with *> file.txt.

# fields never printed: the person's network, home and interests
$script:BenchPrivate = @('ssid', 'nets', 'lat', 'lon', 'place', 'country', 'cc', 'key', 'watch')

# fields that change by themselves, left out of every compare
$script:BenchMoving = @('uptime', 'ran', 'reset', 'utc', 'newest', 'version', 'cc')

# numeric fields that one SET takes back in the form GET says them (main.c, on_line and say_cfg)
$script:BenchRestorable = @('look', 'range', 'theme', 'up', 'flip', 'map', 'ground', 'labels', 'dist_unit',
    'alt_unit', 'vectors', 'beam_step', 'clock', 'block', 'autopilot', 'lost', 'conflict', 'fl_lo', 'fl_hi',
    'bare', 'alerts', 'names', 'temp_unit', 'pressure_unit', 'wind_unit', 'rain_unit', 'backlight', 'night',
    'led', 'led_level', 'led_hold', 'led_near', 'led_red')

# a restart, a panic, a watchdog, a backtrace
$script:BenchCrash = 'Guru Meditation|stack overflow|abort\(\) was called|assert failed|CORRUPT HEAP|Backtrace:|' +
    'rst:0x[0-9a-f]+|task_wdt|Task watchdog|Rebooting\.\.\.|register dump|EXCVADDR|EXCCAUSE|ELF file SHA256|^PC\s*:'

$script:BenchInvariant = [System.Globalization.CultureInfo]::InvariantCulture
$script:BenchUntilSeen = $false

# a port at 115200. -Restart: DTR and RTS up at the open, both down 150 ms later, which restarted the
# CH340 boards of the bench every time. without it both are low before the open, and those boards went on
# as they were. a board on the chip's own USB may differ: check uptime with get-fields.ps1 if it matters
function Open-Board {
    param([string]$Port, [switch]$Restart)
    if (-not $Port) {
        throw 'give -Port, the board''s serial port'
    }
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200
    $sp.Encoding = [System.Text.Encoding]::UTF8
    $sp.ReadTimeout = 200
    $sp.WriteTimeout = 2000
    $sp.NewLine = "`n"
    if ($Restart) {
        $sp.DtrEnable = $true
        $sp.RtsEnable = $true
    } else {
        $sp.DtrEnable = $false
        $sp.RtsEnable = $false
    }
    $sp.Open()
    if ($Restart) {
        Start-Sleep -Milliseconds 150
        $sp.DtrEnable = $false
        $sp.RtsEnable = $false
    }
    return $sp
}

# restart a board on a port that is open: EN pulled low through RTS for 120 ms, as esptool's hard reset does
function Restart-Board {
    param([System.IO.Ports.SerialPort]$Sp)
    $Sp.DtrEnable = $false
    $Sp.RtsEnable = $true
    Start-Sleep -Milliseconds 120
    $Sp.RtsEnable = $false
}

# one line to the board. a newline first, as the site sends it: bytes left by a restart would otherwise
# glue onto the command. the board skips empty lines
function Send-BoardLine {
    param([System.IO.Ports.SerialPort]$Sp, [string]$Line)
    $Sp.Write("`n" + $Line + "`n")
}

# the next line from the board without its colours and its CR, or $null after 200 ms of nothing
function Read-BoardLine {
    param([System.IO.Ports.SerialPort]$Sp)
    try {
        $l = $Sp.ReadLine()
    } catch [System.TimeoutException] {
        return $null
    }
    return (($l -replace '\x1b\[[0-9;]*m', '').TrimEnd("`r"))
}

# printable ASCII only, at most 160 characters: a restart sends bytes that are not text
function Get-Printable {
    param([string]$Text)
    $t = $Text -replace '[^\x20-\x7E]', ''
    if ($t.Length -gt 160) {
        $t = $t.Substring(0, 160)
    }
    return $t
}

function Format-Seconds {
    param([double]$T)
    return ($T.ToString('0.0', $script:BenchInvariant).PadLeft(7) + ' s')
}

# a number as JSON and GET write it, whatever the PC's own decimal sign is
function Format-Value {
    param($V)
    if ($V -is [double] -or $V -is [decimal] -or $V -is [single]) {
        return $V.ToString('0.#####', $script:BenchInvariant)
    }
    return "$V"
}

function Test-CrashLine {
    param([string]$Line)
    return ($Line -match $script:BenchCrash)
}

# a line that names nothing of the person: no web address (the requests carry the home), no quoted text
# (the firmware quotes network names), no network, no home, no watch list, no IP address, no number
# with four or more decimals (a coordinate)
function Test-SafeLine {
    param([string]$Line)
    $risky = 'OWLOS CFG|OWLOS PLANE|OWLOS READY|https?:|"|ssid|network|home|place|watch|' +
        '\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b|\d+\.\d{4,}|\d+/\d+/\d+'
    return ($Line -notmatch $risky)
}

# the known shapes of the firmware's lines, cut to what says nothing of the person.
# returns @{ Topic = ...; Text = ... } or $null. topics: console, frames, memory, map, weather, planes,
# net, epaper, keys, touch, light, crash, warn
function Get-KnownLine {
    param([string]$Line)
    $l = $Line
    if ($l -match 'OWLOS CFG ') {
        return @{ Topic = 'console'; Text = 'OWLOS CFG (not shown)' }
    }
    if ($l -match 'OWLOS (OK|ERR.*)$') {
        return @{ Topic = 'console'; Text = (Get-Printable $Matches[0]) }
    }
    if ($l -match 'OWLOS WXPAGE -?\d+') {
        return @{ Topic = 'console'; Text = $Matches[0] }
    }
    if ($l -match 'OWLOS READY (\S+) (\S+) (\S+)') {
        return @{ Topic = 'console'; Text = "OWLOS READY $($Matches[1]) $($Matches[2]) $($Matches[3]) (MAC not shown)" }
    }
    if ($l -match 'OWLOS READY (\S+) (\S+)') {
        return @{ Topic = 'console'; Text = "OWLOS READY $($Matches[1]) $($Matches[2])" }
    }
    if (Test-CrashLine $l) {
        if ($l -match 'https?:') {
            return @{ Topic = 'crash'; Text = 'RESET OR CRASH (a line with a web address, not shown)' }
        }
        return @{ Topic = 'crash'; Text = 'RESET OR CRASH: ' + (Get-Printable $l) }
    }
    if ($l -match '\d+ fps, \d+ ms a frame, view \d+(, \d+ planes, heap \d+)?') {
        return @{ Topic = 'frames'; Text = $Matches[0] }
    }
    if ($l -match 'ms a frame: background \d+, disc \d+, compass \d+, planes \d+, panel \d+, the rest \d+, sending \d+, touch \d+') {
        return @{ Topic = 'frames'; Text = $Matches[0] }
    }
    if ($l -match 'the second core (draws every other strip|draws the lower half of every strip|stops drawing), heap \d+') {
        return @{ Topic = 'frames'; Text = $Matches[0] }
    }
    if ($l -match "the second core draws: waited for it \d+ ms a frame, its stack \d+ bytes free(, \d+ rows of \d+ this core's)?") {
        return @{ Topic = 'frames'; Text = $Matches[0] }
    }
    if ($l -match 'RAM \d+ free, \d+ in one piece, \d+ at the least') {
        return @{ Topic = 'memory'; Text = $Matches[0] }
    }
    if ($l -match 'map: zoom \d+, \d+ of \d+ tiles, made in \d+ ms[, a-z0-9]*') {
        return @{ Topic = 'map'; Text = $Matches[0] }
    }
    if ($l -match 'map: (no memory[^"]*|\d+ kept tiles not readable[^"]*)$') {
        return @{ Topic = 'map'; Text = 'map: ' + $Matches[1] }
    }
    if ($l -match '(weather( warning)?: .*)$' -and (Test-SafeLine $Matches[1])) {
        return @{ Topic = 'weather'; Text = $Matches[1] }
    }
    if ($l -match '([A-Z][A-Z0-9.]*): HTTP (-?\d+), (-?\d+) bytes in (\d+) ms, (\d+) planes in it') {
        return @{ Topic = 'planes'; Text = "$($Matches[1]) HTTP $($Matches[2]), $($Matches[3]) bytes, $($Matches[4]) ms, $($Matches[5]) planes" }
    }
    if ($l -match "the site's version: HTTP -?\d+") {
        return @{ Topic = 'net'; Text = $Matches[0] }
    }
    if ($l -match 'counted on the map of boards: HTTP -?\d+') {
        return @{ Topic = 'net'; Text = $Matches[0] }
    }
    if ($l -match 'WiFi lost \(reason \d+\)') {
        return @{ Topic = 'net'; Text = $Matches[0] }
    }
    if ($l -match 'WiFi up,') {
        return @{ Topic = 'net'; Text = 'WiFi up (address and name not shown)' }
    }
    if ($l -match 'WiFi started, .*, (\d+) kept') {
        return @{ Topic = 'net'; Text = "WiFi started, $($Matches[1]) networks kept" }
    }
    if ($l -match 'looked round: \d+ networks(, the scan failed)?') {
        return @{ Topic = 'net'; Text = $Matches[0] }
    }
    if ($l -match 'on to network') {
        return @{ Topic = 'net'; Text = 'on to the next kept network' }
    }
    if ($l -match 'asking for the time') {
        return @{ Topic = 'net'; Text = 'asking for the time' }
    }
    if ($l -match '4-grey refresh, \d+ ms \(\d+ px of grey were wrong\)') {
        return @{ Topic = 'epaper'; Text = $Matches[0] }
    }
    if ($l -match 'drawn on top, \d+ px, \d+ ms, grey debt \d+') {
        return @{ Topic = 'epaper'; Text = $Matches[0] }
    }
    if ($l -match '(grey waveform [^"]*|grey test: [^"]*)$') {
        return @{ Topic = 'epaper'; Text = $Matches[1] }
    }
    if ($l -match 'key \d+: look \d+, view \d+, range [0-9.]+|key \d+ gathered|keys taken after the quiet: [a-z ]+; look \d+, range [0-9.]+, page \d+') {
        return @{ Topic = 'keys'; Text = $Matches[0] }
    }
    if ($l -match 'touch: ([^"]*)$' -and (Test-SafeLine $Matches[1])) {
        return @{ Topic = 'touch'; Text = 'touch: ' + $Matches[1] }
    }
    if ($l -match "the screen's light: \d+%|the sun at home: (up|down)[a-z ,']*") {
        return @{ Topic = 'light'; Text = $Matches[0] }
    }
    if ($l -match '^[EW] \(\d+\) ' -and (Test-SafeLine $l)) {
        return @{ Topic = 'warn'; Text = (Get-Printable $l) }
    }
    return $null
}

# listen for some seconds and print the known lines of the topics asked ('all' for every topic).
# -OnLine: a scriptblock given each raw line first; what it returns is printed instead.
# -Writer: the printed lines also go there. -Until: a pattern; the first line that matches ends the
# listening and sets $script:BenchUntilSeen
function Watch-Board {
    param(
        [System.IO.Ports.SerialPort]$Sp,
        [double]$Seconds,
        [string[]]$Topics = @('console', 'crash'),
        [datetime]$Since = (Get-Date),
        [scriptblock]$OnLine = $null,
        [System.IO.TextWriter]$Writer = $null,
        [string]$Until = ''
    )
    $end = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $end) {
        $l = Read-BoardLine $Sp
        if ($null -eq $l) {
            continue
        }
        $said = $null
        if ($OnLine) {
            $said = & $OnLine $l
        }
        if (-not $said) {
            $k = Get-KnownLine $l
            if ($k -and (($Topics -contains $k.Topic) -or ($Topics -contains 'all'))) {
                $said = $k.Text
            }
        }
        if ($said) {
            $text = (Format-Seconds ((Get-Date) - $Since).TotalSeconds) + '  ' + $said
            $text
            if ($Writer) {
                $Writer.WriteLine($text)
            }
        }
        if ($Until -and $l -match $Until) {
            $script:BenchUntilSeen = $true
            return
        }
    }
}

# OWLOS GET, the CFG line as an object, or $null. the raw JSON stays in $script:BenchCfgJson for a rig
# that saves it to a local file. nothing of it is printed
function Get-BoardCfg {
    param([System.IO.Ports.SerialPort]$Sp, [int]$Tries = 3, [double]$Wait = 4)
    for ($i = 0; $i -lt $Tries; $i++) {
        Send-BoardLine $Sp 'OWLOS GET'
        $end = (Get-Date).AddSeconds($Wait)
        while ((Get-Date) -lt $end) {
            $l = Read-BoardLine $Sp
            if ($null -eq $l) {
                continue
            }
            $at = $l.IndexOf('OWLOS CFG {')
            if ($at -ge 0) {
                $json = $l.Substring($at + 10)
                $cfg = $null
                try {
                    $cfg = $json | ConvertFrom-Json
                } catch {
                    Write-Host '  a CFG line came cut or broken: asking again'
                }
                if ($null -ne $cfg) {
                    $script:BenchCfgJson = $json
                    return $cfg
                }
                break
            }
            if (Test-CrashLine $l) {
                Write-Host ('  RESET OR CRASH: ' + (Get-Printable $l))
            }
        }
    }
    return $null
}

# "name=value" for the fields named; the private ones are refused
function Get-CfgText {
    param($Cfg, [string[]]$Names)
    $parts = @()
    foreach ($n in $Names) {
        if ($script:BenchPrivate -contains $n) {
            $parts += "$n=(private, not shown)"
        } elseif ($Cfg.PSObject.Properties[$n]) {
            $parts += ("$n=" + (Format-Value $Cfg.$n))
        } else {
            $parts += "$n=(none)"
        }
    }
    return ($parts -join ' ')
}

# OWLOS OK or OWLOS ERR <why> after a SET, or $null when none came
function Wait-BoardAnswer {
    param([System.IO.Ports.SerialPort]$Sp, [double]$Wait = 3)
    $end = (Get-Date).AddSeconds($Wait)
    while ((Get-Date) -lt $end) {
        $l = Read-BoardLine $Sp
        if ($null -eq $l) {
            continue
        }
        if ($l -match 'OWLOS (OK|ERR.*)$') {
            return (Get-Printable $Matches[0])
        }
        if (Test-CrashLine $l) {
            Write-Host ('  RESET OR CRASH: ' + (Get-Printable $l))
        }
    }
    return $null
}

# SET one numeric field and say the board's answer
function Set-BoardField {
    param([System.IO.Ports.SerialPort]$Sp, [string]$Name, $Value)
    Send-BoardLine $Sp ('OWLOS SET {"' + $Name + '":' + (Format-Value $Value) + '}')
    $a = Wait-BoardAnswer $Sp
    if ($null -eq $a) {
        return 'no answer'
    }
    return $a
}

# put back what moved since $Before (a Get-BoardCfg object): every restorable field that differs, in one
# SET, then GET again to see it took. private fields that differ are named, never set
function Restore-BoardCfg {
    param([System.IO.Ports.SerialPort]$Sp, $Before)
    if ($null -eq $Before) {
        Write-Host '  NOT PUT BACK: no CFG line came before the rig began'
        return
    }
    $now = Get-BoardCfg $Sp
    if ($null -eq $now) {
        Write-Host '  NOT PUT BACK: the board gave no CFG line at the end'
        return
    }
    $parts = @()
    $names = @()
    foreach ($n in $script:BenchRestorable) {
        $was = $Before.PSObject.Properties[$n]
        $is = $now.PSObject.Properties[$n]
        if ($null -eq $was -or $null -eq $is) {
            continue
        }
        if ((Format-Value $was.Value) -ne (Format-Value $is.Value)) {
            $parts += ('"' + $n + '":' + (Format-Value $was.Value))
            $names += $n
        }
    }
    $other = @()
    foreach ($p in $Before.PSObject.Properties) {
        if ($script:BenchRestorable -contains $p.Name -or $script:BenchMoving -contains $p.Name) {
            continue
        }
        $q = $now.PSObject.Properties[$p.Name]
        if ($null -ne $q -and ($p.Value | ConvertTo-Json -Compress -Depth 4) -ne ($q.Value | ConvertTo-Json -Compress -Depth 4)) {
            $other += $p.Name
        }
    }
    if ($other.Count) {
        Write-Host "  DIFFERS and not put back by a rig: $($other -join ', ')"
    }
    if (-not $parts.Count) {
        Write-Host '  every setting as it was found'
        return
    }
    Send-BoardLine $Sp ('OWLOS SET {' + ($parts -join ',') + '}')
    $a = Wait-BoardAnswer $Sp
    $after = Get-BoardCfg $Sp
    $still = @()
    foreach ($n in $names) {
        if ($null -eq $after -or (Format-Value $after.$n) -ne (Format-Value $Before.$n)) {
            $still += $n
        }
    }
    if ($still.Count) {
        Write-Host "  NOT PUT BACK ($a): $($still -join ', ')"
    } else {
        Write-Host "  put back as found: $($names -join ', ')"
    }
}

# the WEATHER look's page (0 the dial, 1 pressure, 2 temperature), or -1. it is not in the CFG line: the
# board says OWLOS WXPAGE while a page listens (SKY ON), and again after a GET. SKY ON also makes the
# board send the planes it hears with their positions, which give away the home's area: nothing of
# that is printed, and SKY OFF ends it
function Get-BoardWxPage {
    param([System.IO.Ports.SerialPort]$Sp)
    Send-BoardLine $Sp 'OWLOS SKY ON'
    Start-Sleep -Milliseconds 300
    Send-BoardLine $Sp 'OWLOS GET'
    $page = -1
    $end = (Get-Date).AddSeconds(4)
    while ((Get-Date) -lt $end -and $page -lt 0) {
        $l = Read-BoardLine $Sp
        if ($null -ne $l -and $l -match 'OWLOS WXPAGE (\d+)') {
            $page = [int]$Matches[1]
        }
    }
    Send-BoardLine $Sp 'OWLOS SKY OFF'
    return $page
}

# IDF's own Python for esptool: -Python, else the one IDF's export set, else the IDF 5.5 installer's,
# else python on PATH (pip install esptool)
function Get-EspPython {
    param([string]$Python)
    if ($Python) {
        return $Python
    }
    if ($env:IDF_PYTHON_ENV_PATH) {
        $p = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
        if (Test-Path $p) {
            return $p
        }
    }
    $p = Join-Path $env:USERPROFILE '.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
    if (Test-Path $p) {
        return $p
    }
    return 'python'
}

# is the board on this port the one meant? esptool chip_id, asked with no --chip, so esptool finds the
# chip itself. the MAC must match; the chip too when -Chip is given (esp32, esp32c3, esp32s3, esp32c5,
# esp32p4). any doubt is a refusal. esptool resets the board on the way in and out, so this is also how
# boot-listen.ps1 and touch-boot.ps1 restart it. made for esptool 4.10's words ("Chip is ...", "MAC: ...")
function Test-BoardIdentity {
    param([string]$Port, [string]$Mac, [string]$Chip = '', [string]$Python = '')
    $py = Get-EspPython $Python
    $o = (& $py -m esptool -p $Port chip_id 2>&1 | Out-String)
    $seenMac = ''
    if ($o -match 'MAC:\s*([0-9a-fA-F:]{17})') {
        $seenMac = ($Matches[1] -replace '[^0-9a-fA-F]', '').ToLower()
    }
    $seenChip = ''
    if ($o -match 'Chip is (ESP32[^\s(]*)') {
        $seenChip = $Matches[1]
    }
    $meant = ($Mac -replace '[^0-9a-fA-F]', '').ToLower()
    if ($seenMac -eq '' -or $meant.Length -ne 12 -or $seenMac -ne $meant) {
        Write-Host "  REFUSED: the board on $Port is not the one meant (its MAC differs, or esptool gave none)"
        return $false
    }
    if ($Chip) {
        $want = @{ 'esp32' = '^ESP32-D0WD'; 'esp32c3' = '^ESP32-C3'; 'esp32s3' = '^ESP32-S3';
                   'esp32c5' = '^ESP32-C5'; 'esp32p4' = '^ESP32-P4' }[$Chip]
        if (-not $want -or $seenChip -eq '' -or $seenChip -notmatch $want) {
            Write-Host "  REFUSED: $Port has chip '$seenChip', meant $Chip"
            return $false
        }
    }
    return $true
}
