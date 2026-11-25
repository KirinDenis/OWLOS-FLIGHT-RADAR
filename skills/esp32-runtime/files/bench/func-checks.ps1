# func-checks.ps1: the functional pass on one running board after a new build: every console rig in turn,
# each printing only its own filtered words.
#
# Run (COM5 and the MAC are examples; the MAC comes from your own notes):
#   & .\func-checks.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff
#   & .\func-checks.ps1 -Port COM5 -Mac aa:bb:cc:dd:ee:ff -Chip esp32 -Dim -Boots 10 *> func-COM5.txt
# Needs: Windows PowerShell 5.1, the rigs of this folder beside it, esptool for touch-boot.ps1, OWLOS
#   Flight Radar on the board with its WiFi. -Dim only for a board whose light turns down.
# Changes on the board: what each rig changes, each saved first and put back by that rig (see its own
#   header). The board is restarted by screen-light.ps1 (with -Dim) and -Boots times by touch-boot.ps1.
# Prints: a === line before each rig, then its own output. Read it for: OK where a value was valid, a
#   value out of range left unchanged, WXPAGE once per change, no RESET OR CRASH outside the restarts the
#   rigs made, "the look changed after 0", "put back as found" or "every setting as it was found".
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [Parameter(Mandatory = $true)][string]$Mac,
    [string]$Chip = '',
    [switch]$Dim,
    [int]$Boots = 5,
    [string]$Python = ''
)
$r = $PSScriptRoot
'=== weather units'
& "$r\weather-units.ps1" -Port $Port -Settle 5
'=== weather page'
& "$r\weather-page.ps1" -Port $Port -Settle 5
if ($Dim) {
    '=== screen light'
    & "$r\screen-light.ps1" -Port $Port
}
'=== weather pages, with and without the panel'
& "$r\weather-pages.ps1" -Port $Port -WithBare -Each 20
'=== look timing'
& "$r\look-timing.ps1" -Port $Port -Looks 0, 1, 2, 3
'=== looks stress'
& "$r\looks-stress.ps1" -Port $Port -Seconds 120
'=== touch at boot'
& "$r\touch-boot.ps1" -Port $Port -Mac $Mac -Chip $Chip -Times $Boots -Python $Python
'=== the LED test'
& "$r\get-fields.ps1" -Port $Port -Names led, look, alerts -LedTest
'=== func done'
