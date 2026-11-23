# Researching a board with no board on the desk

Learned from 2026-11-02 to 2026-11-22, answering "will it run on X" from group posts and GitHub issues, and building eight
boards nobody here has (OWLOS Flight Radar's 7 to 11, 13, 14 and 19). What a build needs is the chip, the screen chip,
the touch chip and every pin. Everything below is how to get those right from far away.

## First find out what it really is

A shop title is noise. The same board is sold under many names, one name covers several boards, and a shop's option can be only an add-on.
Cases from real asks:

- "ELECROW ESP32 Display 320x240, 2.8" HMI Advanced ESP32-S3 IPS Touch Display, Support AI Speech Interaction" is the CrowPanel Advance
  2.8. The plain CrowPanel 2.8 HMI is another board on another chip: classic ESP32, ILI9341V, XPT2046.
- "ESP32 Bruce CYD 2432S028 ... NFC PN532 GNSS ATGM336H" is a plain CYD with modules added. The options on the page are modules, not a variant
  of the board. Check what the variant really is before saying anything about it.
- "ESP32 ILI9341" from Elegoo on Amazon is LCDWIKI's E32R28T, with the 2.8" CYD's pins and one LED on another pin.
- A 2432S028 with no letter: R (resistive) or C (capacitive) is unknown, and the listing will not say.
- A board reported as ESP32-2432S028 had a C3 on it: the ROM line said esp32c3. It took the round board's build. The chip's own words beat
  the sticker: when someone has the board, ask for the first lines the flasher or the serial port prints.
- A "3.5" CYD" on the desk for weeks was an LCDWIKI E32R35T, not a Sunton: only the LED's red gave it away.
- The two "2.8" CYDs" on the desk were LCDWIKI E32R28T too: a photo of the back (USB-C, a BAT socket, "ESP32-32E" printed)
  settled it, after their speakers stayed silent until GPIO 4 was held low. Even boards you own can be misnamed.
- The maker's repository can be gone: RockBase-iot/NM-CYD-C5 gives 404, its forks keep it.
- "ELECROW CrowPanel 7" ESP32 Display Touchscreen 800x480 ... IPS": Elecrow sells a plain 7.0 (N4R8, TN, pins on the S3)
  and an Advance 7.0 (N16R8, IPS, another controller), and shops call both IPS. The SKU on the back (DIS08070H) and its
  version (V3.0) told which.
- "Elecrow advanced 5"": the Advance 5.0 (DIS02050A) differs by board version, V1.0 to V1.3, printed on the board; a
  plain CrowPanel 5.0 (DIS07050H) is another board. One contributor's first port was written while the board was
  misidentified, against the wrong one.
- "older Guition 4.3" ESP32 modules": quite different boards answer to that (an 800x480 RGB one, a 480x272 RGB one, a
  480x272 QSPI one). Asking back, in steps, gave the model (ESP32-4827S043), then "it is a touch screen", then "I
  believe capacitive".
- A board with no letter printed (C, R or N): ask what touch it has and for a photo of the back (the touch chip, the
  module's label).
- Two boards with the same chips on other pins (NM-CYD-C5 and ZLX-ESP32-2, both C5 and ST7789 and XPT2046): never take
  one's build for the other's on the chips alone.

## The trust order

1. Working example code that someone ran on that board.
2. Schematic nets.
3. Generated docs: AI-written guides, auto-generated pin tables.
4. Shop text.

Within 1, always follow which code path actually runs, not which file exists.

The case that taught it: the Elecrow CrowPanel Advance repository has an AI-written hardware guide that says the touch is a GT911, because a
leftover `touch.h` starts one at 0x5D. The sketch itself reads touch through `my_touchpad_read`, which calls `gfx.getTouch`, and `gfx` is
LovyanGFX with a `Touch_FT5x06` at 0x38. The wiki also says 0x38. The repository's datasheets folder holds both an FT6336U and a GT911 sheet, so
even that proves nothing. The real path was found by reading who calls what, starting from the function the GUI library is given. Do that for each
pin: from the setup call back to the number.

Two sources agreeing is only worth more if they are independent. Bruce's port and a maker's Arduino setup may be copies of each other, a
schematic and a sketch are not. Where they disagree, build both as profiles (RGB and BGR on the NM-CYD-C5) instead of choosing.

More cases of the same rule:

- The CrowPanel Advance 5.0's V1.3 hardware guide agrees with the pins and says a model compiled it: the code is the
  source, the guide is not.
- rzeldent's JC8048W550C.json names the low five RGB data lines red; every other setup for those pins, ESPHome's among
  them, has them blue, and on the S3's bus the low five are blue. Built as listed, with the other order behind BOOT.
- A firmware that already runs on the board is working code that has run there: the plain CrowPanel 5.0's pins came from a
  P2000 firmware running on it.
- An ESPHome setup on Home Assistant's forum, "said to work well", is a second source for an RGB board's pins and timing
  (the CrowPanel 7.0). Its notes also carry what broke for others (a touch reset at every start).
- A maker's factory test is working code: the ZLX-ESP32-2's pins.h and config.h, and its TFT_eSPI settings, gave the pins,
  the colour order, the landscape and the clock; the pins in the GitHub issue matched them.
- A board package that tells panel revisions apart at run time (Espressif's for the M5Stack Tab5) beats any table of
  which panel a board has.

A near build flashed on the board is a probe when its owner is willing: what comes up (WiFi and planes on the site, a
picture spilling over the glass) says which parts are the same. See new-board-recipe.md, step 0.

## Schematics in Eagle: read them as text

Eagle .sch files are XML. Fetch the raw file, list `<net name=` and `<part`: the nets name the pins (IO47_TP_INT, IO48_TP_RST) and the parts
name the chips and connectors (two TYPEC parts = two USB-C). No Eagle needed, and no Python: PowerShell does it. Run on 2026-11-04:

```powershell
$h = @{ 'User-Agent' = 'board-research' }
# find the file: anonymous GitHub API
$t = Invoke-RestMethod 'https://api.github.com/repos/<owner>/<repo>/git/trees/master?recursive=1' -Headers $h
$t.tree | Where-Object { $_.path -match '\.(sch|brd|pdf)$' } | ForEach-Object { $_.path }

# spaces and & in the path are %20 and %26
$u = 'https://raw.githubusercontent.com/<owner>/<repo>/master/Eagle_SCH%26PCB/1.2/board%20name.sch'
$x = (Invoke-WebRequest $u -UseBasicParsing -Headers $h).Content
[regex]::Matches($x, '<net name="([^"]+)"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
[regex]::Matches($x, '<part name="([^"]+)"[^>]*deviceset="([^"]*)"') |
    ForEach-Object { '{0} = {1}' -f $_.Groups[1].Value, $_.Groups[2].Value }
```

What it gave on the CrowPanel Advance V1.2: 117 nets, among them IO38_LED_BK, IO40_TFT_CS, IO41_TFT_RS (RS is DC), IO42_TFT_SCK, IO39_TFT_SDA,
IO15_SDA, IO16_SCL, IO47_TP_INT, IO48_TP_RST, IO14_TFT_PWR, USB_D+, USB_D-; parts U1 ESP32-S3-WROOM-1, U4 CH340K, J3 and J4 TYPEC-3.0-16P-SMD, J1
HOLDER-MICROSD-9P, U7 NS4168, FPC1 TFT-2.4/2.8LCD-SPI-10P.

A net name is the maker's own label, and a good one carries the GPIO number. Where it does not, the pin number on the module's symbol does: that
takes the `<pinref>` lines of the net and the module's datasheet.

A repository whose default branch is `main`: change `master` in both URLs. A 404 on the tree means the wrong branch.

## LovyanGFX setup to raw values

Elecrow, LCDWIKI and many others ship a LovyanGFX class. What its fields mean on the wire, from LovyanGFX's Panel_LCD.hpp and Panel_LCD.inl,
read 2026-11-04:

```cpp
enum mad_t { MAD_MY = 0x80, MAD_MX = 0x40, MAD_MV = 0x20, MAD_ML = 0x10,
             MAD_BGR = 0x08, MAD_MH = 0x04, MAD_HF = 0x02, MAD_VF = 0x01, MAD_RGB = 0x00 };

static constexpr uint8_t madctl_table[] = {
                                   0,   // rotation 0
  MAD_MV|MAD_MX|MAD_MH              ,   // 1 = 0x64
         MAD_MX|MAD_MH|MAD_MY|MAD_ML,   // 2 = 0xD4
  MAD_MV|              MAD_MY|MAD_ML,   // 3 = 0xB0
                       MAD_MY|MAD_ML,   // 4 = 0x90
  MAD_MV                            ,   // 5 = 0x20
         MAD_MX|MAD_MH              ,   // 6 = 0x44
  MAD_MV|MAD_MX|MAD_MY|MAD_MH|MAD_ML,   // 7 = 0xF4
};

// update_madctl
writeData(getMadCtl(_internal_rotation) | (_cfg.rgb_order ? MAD_RGB : MAD_BGR), 1);
// setInvert
write_command((invert ^ _cfg.invert) ? CMD_INVON : CMD_INVOFF);
```

So:

- `rgb_order = false` means the BGR bit (0x08) is SET. The name reads the other way round: the trap here. `true` is RGB.
- `invert = true` means INVON (0x21), `false` INVOFF (0x20).
- `offset_rotation = 3`, or rotation 3, gives MV|MY|ML = 0xB0. ML (0x10) only sets the refresh direction, so the picture is the same as 0xA0, which
  is what the firmware uses. With `rgb_order = false` it is 0xB8 / 0xA8.
- Rotation 0 to 3 is the landscape and portrait turns, 4 to 7 the same mirrored. The effective index mixes `setRotation` with `offset_rotation`.
- Panel_ST7789 does not override `getMadCtl`, so this table applies. Check the panel class in question: a class can override it.
- `Panel_ST7789`'s own start-up sends porch, gate, VCOM and gamma values, the firmware's ST7789 start-up does not, it relies on the chip's settings.
  If such a build looks washed out, those values are the next thing to take from Panel_ST7789.hpp. Not seen yet: no ST7789 board here.
- The rest maps one to one: bus `pin_sclk`, `pin_mosi`, `pin_miso`, `pin_dc`, `freq_write`; panel `pin_cs`, `pin_rst`; `Light_PWM` `pin_bl`;
  touch `i2c_addr`, `pin_sda`, `pin_scl`, `pin_int`, `pin_rst`.

## rzeldent's board JSONs to raw values

github.com/rzeldent/platformio-espressif32-sunton: one JSON per board, everything in `build.extra_flags`.

- `<CHIP>_SPI_BUS_MOSI`, `_MISO`, `_SCLK`, `<CHIP>_SPI_CONFIG_CS`, `_DC`, `DISPLAY_BCKL`: pins.
- `<CHIP>_DEV_CONFIG_COLOR_SPACE=ESP_LCD_COLOR_SPACE_BGR` or `_RGB`: the MADCTL BGR bit.
- `DISPLAY_SWAP_XY`, `DISPLAY_MIRROR_X`, `_Y`: for portrait. v1 and v2 have MIRROR_X true, v3 false: that is the "one axis" of the 2.8".
- `DISPLAY_IPS`: a flag on the IPS panels, by its use it turns inversion on. Its code was not re-read for this skill.
- `<CHIP>_DEV_CONFIG_VENDOR_CONFIG`: a whole init list when the panel needs one (the Rv2: that is where our INIT_V2 came from), NULL otherwise.
- `XPT2046_SPI_HOST=SPI2_HOST` with the LCD's bus pins means touch on the LCD bus. SPI3_HOST with other pins (2.8") means its own.
- `GT911_IO_I2C_CONFIG_DEV_ADDRESS_5D`, `CST816S_...ADDRESS_15`: the address.
- `*_PCLK_HZ` is what they run, usually a safe 24 MHz.

Its README's table has slips: JC8048W550C appears as 7.0" with SPI touch, the JSON says GT911 on I2C and the name says 5". The JSON is the working
one, the table is text: the trust order again.

One disagreement to remember: esp32-2432S028Rv3.json has no `DISPLAY_IPS` and a NULL vendor config, while the firmware's v3 start-up has
inversion on. That came from a viewer's v3: on the no-inversion start-up a dark UI came out light blue on white. It is built, not measured here. If
a v3 shows a negative, that line is the first suspect.

## Where pins came from

- The maker's wiki and repository: Elecrow (wiki and Elecrow-RD on GitHub, example code, Eagle files, datasheets folder), LCDWIKI (lcdwiki.com, a page per
  board with a pin table, schematic and start-up code).
- Forks, when a maker's repository is gone: github.com/michaelhenry/NM-CYD-C5, Documention/2-sch.
- Bruce firmware, github.com/BruceDevices/firmware, the boards/ folder: a port per board, many said to be run on the real thing. Its boards list is in
  boards-others.md. Also a good guide to which pins a board's add-ons take.
- rzeldent/platformio-espressif32-sunton: Sunton and Guition boards, the three 2.8" revisions.
- ViewOwl and the older OWLOS firmware for what is already on the desk: TFT_eSPI `User_Setup.h` in OWLOSAirQuality gave the touch's CS 17.
- A maker's small repository with a pin list and factory tests (github.com/qmaakdfq/ESP32-C5-Series-Development-Board for
  the ZLX-ESP32-2: GPIO_Pinout_CN_EN.md, pins.h, config.h).
- A GitHub issue with the pins copied from a maker's manual (OWLOS Flight Radar issue #13): check them against the maker's
  own files before building.
- ESPHome setups, on the Home Assistant forum and ESPHome's device pages: RGB timing and sync pins, what broke for others.
- Firmware already running on the board (a P2000 firmware for the plain CrowPanel 5.0), and other projects' configs
  (FusionEdge's `<board>_options/myoptions.h`: one header of pins a board, with a README of what was tested).
- Espressif's and makers' board packages in the component registry (espressif/m5stack_tab5): the panel, touch and
  power switches as code, with revisions told apart at run time.
- Contributors' libraries (github.com/chipguyhere: board and display libraries for ESP32-S3 RGB and ESP32-P4 boards).
- github.com/AungMoeKhine/AMK_CYD_Universal_TFT, read only (MIT; part of it is chipguy_* files, check whose before copying
  any code): run-time detection of CYD variants (the panel's ID over MISO or GPIO 13, the backlight pin by a pull-down,
  the light sensor on 34, capacitive against resistive), a table of 2.4", 3.2", 4.0" CYDs, the capacitive ones, the
  Guition JC2432W328, Waveshare and Sunton RGB boards. Its README says green and blue swap on the ESP32-024 and ESP32-035
  (docs, not checked). Its RGB driver has notes on bounce buffer desync and copying in a high-priority task.
- hexeguitar/ESP32_TFT_PIO documents the stock CYD's amplifier.
- Shop listings: a last resort for the chip name only.

Bruce's CYD setup is also a lesson in what add-ons do to a board: the GPS goes on TX/RX, GPIO 1 and 3, the same UART as the USB bridge, so it talks over
flashing and over the settings console. It has to come off for both. The PN532 goes on I2C 27/22, the pins of CN1. Before answering about a kit, read which
pins its modules take.

## Which board a post really means: asking back

- Ask for the model printed on the back, a photo of the back, or the first lines of the serial port.
- Ask for the SKU and the board version printed on it when a maker changes parts between versions (Elecrow's CrowPanels).
- All a build needs is the type and the pinout: say so.
- Ask whether they would test a build. Most did, and their logs found what no reading could (the C5's memory, twice).
- A wiring drawing from a comment: esp32-project-playbook has how to enlarge one. Send the digits back in text and ask for a yes.
- Write it down at once in BOARDS-WANTED.md: chip, screen, touch, pins, source link, whether the asker offers to test. A board nobody here can test
  is worth building only with someone who has it, or on a second ask.

## How to write what was found

Per board, in BOARDS-WANTED.md and board.h's comment, in plain lines:

```text
what it is, with its exact name and what it is NOT
chip, flash, PSRAM, screen, touch, USB
pins, with the source of each group
built with: every default that was a choice, and why
doubts: what the sources disagree on, what looks like a leftover
not driven: what is on the board and left alone
to learn from whoever tries it
```
