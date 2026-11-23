# the boards named, one after the other, each with the repository's BUILD.BAT. a line each: built or not,
# fresh or not, warnings, errors, the app's size, the time. the full logs go beside this script,
# build-<board>.log, or to -Logs
#   build-some.ps1 -Repo <repo> -App owlos-flight-radar -Boards ili9341,st7796,gc9a01
# fresh: the .bin is newer than the start of its build. a failed IDF build leaves the old .bin in place,
# and "built" alone would not show it. run it in PowerShell: BUILD.BAT goes through cmd /c
param([string]$Repo, [string]$App, [string[]]$Boards, [string]$DefaultBoard = 'ili9341', [string]$Logs = '')
if (-not $Logs) { $Logs = Split-Path -Parent $MyInvocation.MyCommand.Path }
foreach ($d in $Boards) {
    $log = Join-Path $Logs "build-$d.log"
    $t0 = Get-Date
    & cmd /c "$Repo\firmware\BUILD.BAT $d" *> $log
    $text = Get-Content $log -Raw
    $ok = $text -match 'Project build complete'
    $warn = ([regex]::Matches($text, 'warning:')).Count
    $errs = ([regex]::Matches($text, ' error: ')).Count
    # the default board keeps the plain build folder it always had, the others build_<board>
    $dir = if ($d -eq $DefaultBoard) { 'build' } else { "build_$d" }
    $bin = Get-Item "$Repo\firmware\$dir\$App.bin" -ErrorAction SilentlyContinue
    $fresh = $bin -and $bin.LastWriteTime -gt $t0
    "{0,-12} built {1} fresh {2} warnings {3} errors {4} bytes {5} in {6:n0} s" -f $d, $ok, $fresh, $warn, $errs, $bin.Length, ((Get-Date) - $t0).TotalSeconds
}
"done"
