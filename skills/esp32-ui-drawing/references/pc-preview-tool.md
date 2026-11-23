# The PC preview, and the tools around it

Every picture of a screen in the READMEs and on the sites comes from the same C that runs on the board. Test there
first: no flashing, and an agent can look at the shots. Nobody needs to click and report. Code:
[code-tools.md](code-tools.md) (BUILD.BAT, the head of preview.c, png.inc, Air Quality's `--prof`, Guide, QrBake).
Films made the same way: films-and-stills.md.

How sure: (M) seen on a real board, (P) built and run on the PC, (R) reasoned.

## tools/preview

`BUILD.BAT [screen]` compiles `preview.c` plus the `core/` sources with MSVC (Visual Studio 2022's C++ tools, through its
`vcvars64.bat`), `/DRADAR_DISPLAY=N`, and runs a scripted session that writes `shots/<screen>/NN-name.png`:

```bat
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"%VER%\" ^
   /Fobuild\%DISP%\ /Febuild\%DISP%\preview.exe preview.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ...
```

- The screens it takes in Flight Radar: ili9341 (the default), st7796, ili9486, gc9a01, crowadv50, esp4827s043, epd400,
  epd792. The scaled one links the glass fonts, the paper ones the paper fonts.
- The version comes from the first `## ` line of CHANGES.md, as the firmware takes it.
- The map's tiles are fetched once by curl into `tools/preview/tiles` (`ground.inc`), so the shots have a real map.
- BUILD.BAT rewrites tracked shots: copy what you need, delete the new ones, restore the rest from git. The repo keeps only
  the shots the README and the site show (a `.gitignore` with one `!` line each), and CI builds the site from what git
  keeps: a picture the site starts to show needs its line.

A session is a list of plain calls: time passes, a finger taps, a key is pressed, a shot is saved.

```c
fly(84);                     // trails grow: reach the board's real state before judging anything
shot("01-demo-sky");
press(1);                    // LIST: a tap at the button's place, through ui_touch
shot("04-list");
drag(120, 180, 120, 40);     // 10 samples, 20 ms apart
pick("OWL77H"); shot("03-picked-heavy");
ui_key(&ui, KEY_OK, &kc);    // paper boards: the keys, through the real ui_key
shot("60-paper-picked");
```

- `shot()` renders strip by strip with `gfx_pack565` and unpacks 565 back to RGB: the PNG is what the panel shows,
  dither included. 2x zoom, nearest neighbour; 1x on scaled glass (it is big enough). On e-paper it packs with
  `paper_pack` and writes each 2-bit code as the panel's real grey (`PAPER_LEVEL`).
- A tap is 4 down samples and a lift through the real `ui_touch`: it tests the hit areas too. `finger()` sends a path,
  a sample every 20 ms, like the touch task.
- Demo data goes through the real parser, in 37 byte pieces, like the network hands it over. FR24's own format too.
- Weather: a stormy day built in the script (a front in the evening, the pressure falling 11 hPa), the day before
  filled hour by hour for the dial's yesterday loop and the graphs' past, warnings on and off, paper style 0 and 1.
- `no_tiles`: as a board with no WiFi and no tiles kept.
- The PNG writer is `png.inc`, its own deflate, about 160 lines, no zlib.
- WiFi Radar's preview also prints turn and walk errors against the known truth.

Shot names in Flight Radar, to know what to look at: 00 boot, 01 demo sky, 02 live, 03 to 09 picked, east up, range,
emergency, FR24 route; 10 and 11 setup and its tabs; 12 no WiFi; 13 touch alignment; 14 to 17 turning and zooming; 18
hue; 19 the round's menu; 20 to 28 the map looks and the panel; 30 to 37 the WiFi pages and keyboards; 38 to 41 ABOUT and
the new-version note; 42 to 44 the radar's options and the panel away; 50 to 57 the weather's warnings and the colour
dial with its column (`57-weather-hud`); 60 to 66 the paper's keys, pressed and gathered, plain sheet; 67 to 72 the paper
weather (card, graphs, dial).

## Proving things with it

- **A speedup changed nothing**: keep the PNGs, compare bytes after the change. MAP look 110 to 81 ms, shots
  pixel-identical (M on the board, P on the shots).
- **A new size did not touch the old one**: 320x240 stays pixel-identical. The scaled layouts compiled every other screen
  to the same picture: 135 shots compared (P).
- **A refactor did not change paper**: the paper weather was checked pixel-identical against saved previews after each
  step of the palette roles (P).
- **Strips only read**: draw each look and page whole, in strips, and in two halves cut at every row, and compare. 0 px
  differed on the 2.8", 3.5", 5", 4.3" and round layouts (P). A 1 px difference in LIST on boards that pair strips was
  found this way.
- **`--prof`** (Air Quality): each part of a page switched off in turn by `g_prof_off`, over 200 frames. Only proportions
  mean anything for the ESP32, absolute numbers do not. It runs a simulated day first, so plots are full.
- **The C3's way**: define `GFX_SOFT_FLOAT=1` on the PC.
- **Measure what the eye sees**: for motion, record real data and replay it through the core on the PC (positions,
  nose, steps back, worst frame), not only made-up paths (animation.md).
- Read the shots: `shots/` is for looking. A layout bug is seen in a second.

Trap, a PC profile that lies (M): the slow state only comes after 80 s of flying. Make the run reach the device's state.

## The same core in a web page

Flight Radar's `web/twin.c` builds `core/` with Emscripten into the site's page: the board's own screen code runs in the
browser and stays in step with the board over Web Serial (the board sends its planes as lines of the same JSON, both
sides draw, only inputs and settings travel; pixels cannot: 115200 baud is about 11 KB/s, a 320x240 frame 150 KB). In
the page the board has the WiFi, so there is no keyboard (`RADAR_WEB`). It is the preview's sibling: a screen anyone
can open (M, clicked through against a real board). The site side is in
[esp32-flash-and-settings](../../esp32-flash-and-settings/SKILL.md).

## tools/Guide: where to tap

The README's pictures of where to tap. `GUIDE.BAT` reads `notes.txt` and draws words at the sides of a preview shot
with arrows to screen coordinates:

```
image st7796/02-live.png radar.png
# a spot, ringed: side, x, y, words. '|' breaks a line
L 49 55 DRAG THE RIM:|THE MAP TURNS
# a button, framed: its top left corner, its size
L 6 300 144x18 COLOUR IS HEIGHT
R 311 282 162x32 HUE: COLOURS|LIST: EVERY FLIGHT|SET: SETTINGS
```

Notes on a side are stacked by height and pushed apart. A dark edge under each arrow keeps it seen on the light map.
Run the previews first, the pictures follow any change to the screens with no hand work. Colours:
themes-and-palettes.md.

## tools/FontBake, tools/QrBake

FontBake: fonts-and-font-baking.md. QrBake: `QrBake <url> <core folder>` writes `core/qr_site.c` (first-run-flow.md).
C#, .NET 8. All converters are C#, not Python.

## Agents in the loop

An agent can build the preview, read the PNGs and judge a layout without a person. It cannot see an LED or the glass's
real contrast: for those ask a person a yes or no question, or a photo (rgb-led-as-ux.md, e-paper-design.md). Never ask
a person to click through and report what an agent could render.
