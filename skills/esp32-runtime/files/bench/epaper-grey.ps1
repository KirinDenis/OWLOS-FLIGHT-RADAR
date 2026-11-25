# epaper-grey.ps1: which bytes of the e-paper's grey waveform give four greys the eye can tell apart? Change
# a few bytes, print the test bands with them, and photograph the glass.
#
# Run (COM5 is an example, give your board's port):
#   & .\epaper-grey.ps1 -Port COM5                              the bands with the waveform as it is
#   & .\epaper-grey.ps1 -Port COM5 -Pokes "141:47,99:49"        those bytes changed first
# A poke is index:value, the index in decimal, the value in hex: "141:47" sends OWLOS EPDLUT 141 47. What the
#   bytes mean (firmware/main/epd.c, above LUT_4GRAY): 5 blocks of 6 groups of 7 bytes, then the frame rate.
#   A phase byte has its voltage in the top 2 bits (00 none, 01 towards white, 10 towards black) and its
#   frames in the low 6. A grey is its third group: frames towards white, then towards black.
# Needs: Windows PowerShell 5.1, bench-serial.ps1 beside it, an e-paper build of OWLOS Flight Radar
#   (OWLOS EPDLUT and EPDTEST exist only there), a camera and a person to take the photo.
# Changes on the board: the waveform in RAM only, never in flash. It starts from the panel's own (OWLOS
#   EPDLUT all) and is put back to the panel's own at the end, also on Ctrl+C; a restart does the same.
#   OWLOS EPDTEST prints six bands (black, dark grey, light grey, white, dots 1/8, dots 1/2) that stay on
#   the glass for 10 minutes, then the radar comes back whole. No restart.
# Prints: the board's lines about the waveform (each byte, old -> new), the test's own line with its time,
#   the refreshes, a restart or crash. The values that look right go into LUT_4GRAY in epd.c by hand.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$Pokes = '',
    [int]$Listen = 20
)
. (Join-Path $PSScriptRoot 'bench-serial.ps1')
$topics = @('epaper', 'crash')
$sp = Open-Board $Port
$t0 = Get-Date
try {
    # "OWLOS EPDLUT" needs a word after it: a byte with no value, or a word that is no number, means all back
    Send-BoardLine $sp 'OWLOS EPDLUT all'
    Watch-Board $sp 0.5 $topics $t0
    if ($Pokes) {
        foreach ($p in $Pokes.Split(',')) {
            $i, $v = $p.Trim().Split(':')
            if ($i -notmatch '^\d{1,3}$' -or $v -notmatch '^[0-9a-fA-F]{1,2}$') {
                "${Port}: '$p' is not index:hexvalue, skipped"
                continue
            }
            Send-BoardLine $sp "OWLOS EPDLUT $i $v"
            Watch-Board $sp 0.4 $topics $t0
        }
    }
    Send-BoardLine $sp 'OWLOS EPDTEST'
    Watch-Board $sp $Listen $topics $t0
} finally {
    Send-BoardLine $sp 'OWLOS EPDLUT all'
    Watch-Board $sp 0.5 $topics $t0
    $sp.Close()
}
"$Port done: the bands stay on the glass for 10 minutes, the waveform is the panel's own again"
