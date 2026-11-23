# "Bruce CYD 2432S028" kits on AliExpress (takes build `ili9341`)

A listing "ESP32 Bruce CYD 2432S028 ... NFC PN532 GNSS ATGM336H". It reads as the plain 2.8" CYD sold for the Bruce
firmware, with add-on modules: the options on the page are modules, not variants of the board.

- Build: none of its own, it takes `ili9341` ([ili9341](ili9341.md)). Nothing to build.
- How sure: docs. Not run on one here. Asked 2026-11-04.
- The listing does not say R (resistive) or C (capacitive), and the build is for R.

## What the add-ons take

Bruce's own setup (github.com/BruceDevices/firmware, boards/CYD-2432S028):

- PN532 NFC on I2C 27 and 22, the pins of CN1. The radar leaves them alone.
- GPS on TX and RX, GPIO 1 and 3: the same UART as the USB bridge. A GPS plugged in there talks over the flashing and
  over the settings console (the site's step 2), so it comes off for both. The radar uses neither module.

The lesson for any kit: read which pins its modules take before answering. Chip and pins: [ili9341](ili9341.md),
[esp32](../chips/esp32.md).
