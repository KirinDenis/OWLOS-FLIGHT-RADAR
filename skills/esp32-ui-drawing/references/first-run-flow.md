# First-run flow

A person with a new board and no manual. What the screen does from power on to a working instrument, and the
ABOUT page with its QR. Code: the alignment page in [code-touch.md](code-touch.md), the demo card and ABOUT in
[code-pages.md](code-pages.md), QrBake in [code-tools.md](code-tools.md). Flashing and WiFi from the site:
[esp32-flash-and-settings](../../esp32-flash-and-settings/SKILL.md).

How sure: (M) seen on a real board, (P) built and seen in the preview, (R) reasoned.

## The order (Flight Radar, M on the CYDs and the round C3)

1. **Power on.** The backlight fades in over a real picture. On the main view a ring opens with the name (1.8 s).
2. **5 crosses**, if no alignment is saved or the saved one looks crazy: one cross at a time, `PRESS AND HOLD THE
   CENTRE OF THE CROSS`, with a nail or stylus. Text sits in the half without the cross. `POINT 2 OF 5`. BOOT skips.
   Median of each press, least squares, refused over 18 px with `TRY AGAIN`. The round glass does it too: it is how
   the touch chip's axes are found. Air Quality takes 9 crosses and a quadratic map for its panel: worst cross 8 px
   (M). Capacitive boards with a fixed map need no crosses (FusionEdge's screen calibrates only an XPT2046).
3. **Boards sold with different screens**: the alignment page and an **8 second BOOT card** say `SCREEN WRONG? PRESS
   BOOT`, `NOW ILI9341 V2`, with a line that shrinks as the seconds go. There a short BOOT starts the screen as the
   next revision, a held one skips the crosses. The owner of a wrong screen sees negative colours or a mirrored
   picture, so the words must be big enough to read through that. The choice is saved. BOOT is the way on purpose: a
   screen select on the site only confused people (M).
4. **Demo sky.** No WiFi set, or the set one gone for over 20 s: a made-up sky flies, through the real parser, with a
   **card under home: DEMO** in the pixel font at scale 4 or 5, so it reads across the room, why (`NO WIFI SET UP`,
   `CAN'T SEE WIFI X`, `X: WRONG PASSWORD?`, in the alarm colour when it is a fault, dim when it is only news), and
   the button **SET UP WIFI**. Top left says `DEMO SKY`. The board is never blank and never lies.
5. **SET UP WIFI**: the networks, strongest first, signal bars, a lock. Tap, type, JOIN, `JOINING THE WIFI`. An open
   network joins at once. Round and no-touch boards say `SEND WIFI FROM THE SITE`.
6. **The DEMO card waits** for the BOOT card's 8 s: the two would cover each other (P: the first seconds had both,
   `demo_shown` holds the DEMO card until the screen-pick window is over).
7. **Later: NEW VERSION note.** The board asks the site for `version.json`. A small note: the panel's title over the
   radar, a card under the town's over the map, under home on round. A tap opens ABOUT. Nothing else changes, nothing
   nags. A weather warning's chip takes the note's corner and the note moves under it.
8. **ABOUT**: version, what the site has (`the newest one`, `0.5 is on the site`, `no WiFi: the site not asked`), the
   board, whether a key is stored, how to update, the address, and a **QR**.

On e-paper with no WiFi and no tiles kept: the demo sky on white paper, the home card says `NO WIFI SET UP` and a
chip `MAP NEEDS WIFI` (M, 4.2").

## The QR on ABOUT

The address does not change, so nothing is computed on the board: `tools/QrBake <url> <core folder>` writes
`core/qr_site.c`, one bit a module, 33 x 33, version 4, correction M, 165 bytes. Dark on a light plate with a 2
module edge, whatever the theme, as printed ones are: a phone reads a light code on a dark screen less surely.
Modules 4 px on 320x240, 5 on 480x320, 3 on round. A run of dark modules is one fill. Checked with ZXing on the
preview shots (M, on the shots).

## Ask what decides, before doing anything

Thermo Relay's first question: `WHAT IS ON THE RELAY?` OFF, HEAT, COOL with what each means, `NOTHING SWITCHES UNTIL
YOU PICK ONE`. A fridge starts at 5, a room at 21. Ask the question that decides what the device does, before it
does anything.

## Paper boards

No touch, no crosses: keys. The keys' strip at the edge says what each key does now from the first picture
(e-paper-design.md). The default look on paper boards is CLASSIC, the printed chart (M).
