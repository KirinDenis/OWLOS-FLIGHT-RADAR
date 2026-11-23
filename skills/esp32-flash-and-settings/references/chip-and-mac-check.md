# Flash only after the chip and MAC check

Before anything writes to a board (a flash, an erase, a restore), ask the board who it is and refuse on any
doubt. Real files: `../files/rigs/flash-one.ps1` (flash a finished build), `backup-board.ps1` (read the
whole flash), `flash-board.ps1` (build, then flash), `flash-manifest.ps1` (flash a site folder, backup and
restore). All four do the same check.

## Why

- COM numbers move. The same board comes up on another port after a replug, and on the bench the ports of
  three boards were taken for each other once, so the wrong board got the blame (measured).
- Two boards of a kind look the same, and one of them may hold a person's WiFi and home.
- Flashing the wrong board is the expensive mistake: a firmware that was not backed up first is gone for good.
  It happened once, to a 3.5" board whose earlier firmware was overwritten at the owner's word with no backup.
- An older guard of the bench rigs let a wrong port through once. esptool's own chip check, with `--chip`
  given, stopped the write (measured). The stricter check below came from that: chip and MAC both, and any
  doubt is a refusal.

## The check

1. List the ports: `Get-CimInstance Win32_PnPEntity | Where-Object Name -match 'COM\d'`. The name says
   CH340, CP210x or USB JTAG/serial.
2. Ask the port with NO `--chip`: `python -m esptool -p <port> chip_id`. esptool finds the chip itself, so a
   wrong chip shows instead of being assumed. Read the `Chip is ...` line and the `MAC:` line. On a classic
   ESP32 it warns there is no chip ID and prints the MAC anyway.
3. Compare both with your own list of boards (MAC, chip, build), kept in private notes and never in the
   repository. Both must match exactly. No answer, no MAC line or no chip line is a refusal.
4. Only then write, now with `--chip` given, so esptool itself refuses a chip that is not that one.

The rigs' lines:

```powershell
$o = (& $py -m esptool -p $Port chip_id 2>&1 | Out-String)
$seenMac = if ($o -match 'MAC:\s*([0-9a-f:]{17})') { $Matches[1] } else { '' }
$seenChip = if ($o -match 'Chip is (ESP32[^\s(]*)') { $Matches[1] } else { '' }
$want = @{ 'esp32' = '^ESP32-D0WD'; 'esp32c3' = '^ESP32-C3'; 'esp32s3' = '^ESP32-S3';
           'esp32c5' = '^ESP32-C5'; 'esp32p4' = '^ESP32-P4' }[$Chip]
$macOk = $seenMac -ne '' -and $seenMac -eq $Mac.ToLower()
$chipOk = $seenChip -ne '' -and $want -and $seenChip -match $want
if (-not ($macOk -and $chipOk)) { "NOT FLASHED: ..."; exit 1 }
```

- The classic ESP32 boards here all say `ESP32-D0WD...` (D0WD, D0WDQ6, D0WD-V3). A PICO or U4WDH chip needs its
  own pattern. The ESP32, C3 and S3 lines ran on the bench; the C5 and P4 lines are esptool's names, not run
  here (reasoned).
- Made for esptool 4.10's words. A newer esptool that words its answer otherwise makes the check refuse,
  which is the safe side (reasoned).

## A board that runs our firmware names itself

Since Flight Radar 0.8 the console answers `OWLOS HELLO` with `OWLOS READY flight-radar <version> <build>
<mac>`, the MAC as 12 hex digits with no colons (`usb-console.md`). Before flashing a MAC that is not on your
list, ask HELLO for the build: then you know which firmware to put back. On the P4 the firmware says the P4's
base MAC, because the WiFi's MAC belongs to the C6 beside it; esptool reads the same base MAC from the chip
(reasoned from the code, not compared on a P4 here).

What is on a board without touching NVS, for a board that answers nothing: read the partition table at 0x8000
and the start of the app at 0x10000 (`read_flash 0x8000 0x1000`, `read_flash 0x10000 0x100`). An IDF app
carries its description 32 bytes into the image: a magic word, then the version at +16 and the project name
at +48, 32 bytes each (docs: IDF's `esp_app_desc_t`). A rig on the bench named the app on a board this way and
never read NVS (measured).

## Back up first

```
python -m esptool -p <port> -b 460800 read_flash 0 0x400000 board-before.bin
```

`backup-board.ps1` reads the whole flash only when the MAC matches, never writes over a file that is there,
checks the size that came (4 MB by default, `-Size` for 2, 8 or 16 MB) and prints no settings. A backup holds
the person's WiFi and home: it stays outside every repository and every chat, in a folder no cleanup touches.
A temp folder of the build PC was cleaned once and took working rigs with it (measured).

To put a backup back: `write_flash 0 board-before.bin` after the same check (`flash-manifest.ps1 -Restore`).
Whole 4 MB backups brought two CYDs back from another firmware, and the settings in them came back with them
(measured).

## Ports that are not free

- Opening a port resets a CYD: the CH340's auto-reset wiring pulses EN, and `chip_id` restarts the board. It
  also ends anything the board was doing, so for logs keep ONE long reader instead of short peeks.
- A person's `idf.py monitor` keeps the port busy: read their terminal, never kill their process.
- The Flight Radar site in a browser connects the ports it was allowed before by itself and holds them:
  esptool then says "Access is denied" or "could not open port". Only the person can close that tab
  (measured).
- The flasher's dialog in a browser holds the port until it closes (`web-flasher.md`).
- A killed shell can leave a Python child holding the port. Find that one process and stop it, nothing else.

## The bench rules, in short

- MAC and chip first, a backup first, then the write.
- One program on a port at a time.
- Only the boards meant: ask before touching any other board on the desk.
- Count the hashes after the write (`local-flash.md`, Verifying).
- After an update without erase, compare the settings field by field (`usb-console.md`, the test rig).
