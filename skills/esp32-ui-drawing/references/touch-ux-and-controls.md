# Touch UX and controls

The touch state machine and its numbers, hit areas, gestures on an instrument, the on-screen keyboard, settings
rows, toasts, and what BOOT and keys do. Code: [code-touch.md](code-touch.md). Touch chips and their drivers are in
[esp32-lcd-touch](../../esp32-lcd-touch/SKILL.md) (e.g. [xpt2046.md](../../esp32-lcd-touch/references/touch/xpt2046.md));
this file starts at "raw touch can be read". The first-run alignment is in first-run-flow.md, the e-paper's keys in
e-paper-design.md.

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## The state machine

`ui_touch`, fed with queued samples. The numbers, each with why:

- **Sampling**: every 15 ms, between strips too, queued with times, 24 deep. At 8 fps a swipe gave 1 or 2 points and
  came out as a tap (M).
- **Press point**: the median of the first 3 samples, the hit decided again there. The first readings of a resistive
  press wander (M).
- **Tap**: on lift, not moved, at the median of the samples without the last one, `hit_at` once more. The last sample
  is the lift-off.
- **Moved**: `abs(dx) + abs(dy) > 14`, 8 when a turn or zoom is armed: a turn should start soon, not jump.
- **Hold**: 1200 ms still.
- **Repeat** (+ and -, DEL): first after 450 ms, then 110 ms, after 2.5 s 50 ms. Held + runs and speeds up.
- **Align again**: 4 s still on nothing. Works even when no button can be hit. Also on the weather's dial or graph.
- **Swipe**: on lift, `abs(dx) > 70 && abs(dx) > 2 * abs(dy)`: mostly sideways.
- **Drag scroll**: `scroll = scroll0 - (y - y0)`, clamped, no inertia. A list moves with the finger and stops when it
  stops.
- **Dimmed screen**: the first touch only wakes it, that press is swallowed (`u->swallow`). A palm on a dark screen
  changes nothing.
- **Save**: 3 s after the last change. Holding + is one flash write, not twenty (M).
- **Screen dim**: not while something is wrong. An alarm keeps the screen bright.

## Hit areas

A finger is wide and a resistive panel is a few pixels off. Touch zones are bigger than what is drawn, and they are
separate from drawing: `hit_at` returns a `HIT_*` code for a point, ranges for rows (`HIT_ROW + k`, `HIT_KEY +
cell`).

- Pick the NEAREST thing within a radius, not the first: 18 px in Flight Radar, 14 or 16 in WiFi Radar.
- Buttons over a map answer 8 to 12 px further out than they are drawn, down to `LOOK_Y + LOOK_H + 12` (M: a person
  found them hard to hit).
- Bars answer above their top: `y >= BAR_Y - 4`, tabs 8 px above.
- The hit is stored in `t_hit`, and drawing asks it: `pressed = u->t_down && !u->t_moved && u->t_hit == HIT_X`. A
  button lights under the finger within a frame and goes out the moment the finger moves.
- Ranges grow: SET's rows outgrew their range (44 rows > 40) and the ranges are 60 wide now, with a
  `_Static_assert` in ui_pages.c so it cannot happen silently again (P).
- The weather's dial or graph is one big target (`HIT_WXPAGE`): a tap turns the page. On the round glass a tap on the
  scope in WEATHER turns the page and shows its menu row (P).
- **One button, two ways** (0.17, an owner's idea). The look's button answers by halves: `look_hit` gives
  `HIT_LOOK_BACK` left of its middle and `HIT_LOOK` right of it, so going back is one tap, not a whole round. It is
  drawn as one button with a small arrow at each end (`look_button`), and the pressed half's arrow lights. It grew to
  the left, 14 px, so the seven-letter names keep their size; on the scaled screens 18, because their smooth font
  stands wider than the pixel font it stands in for: with 14 the arrows touched the letters on the 5" (P, the
  preview). Boards without touch keep the old button.

## The ring under the finger

```c
if (u->t_down && u->view != VIEW_CAL) {        // show where touch lands, bad cal is obvious right away
    gfx_ring(cv, (float)u->t_x, (float)u->t_y, 9.0f, 1.2f, C_FOCUS, 210);
    gfx_disc(cv, (float)u->t_x, (float)u->t_y, 1.6f, C_FOCUS, 255);
}
```

Cheap and it pays many times over: touch feels answered at 8 fps, and a bad alignment shows at once.

## Gestures on an instrument (Flight Radar)

One finger does both, by where it starts:

- On the **rim**: turn the map. `up = up0 - da`. The rim lights where the finger is, a line shows where north is now.
  Near north on lift (within 4 degrees) it snaps to north and says `NORTH UP`.
- **Inside**: one-finger pinch against home: `range = range0 * r0 / r`, so what was under the finger stays under it.
  A ring under the finger with the distance over it, on a solid card when a map is under it. The map texture is
  stretched while the finger is down, and made again after. On lift the range snaps to the nearest nice value by ratio
  (`fabsf(logf(v / NICE[i]))`), 44 is nearer 40 than 50, and a toast says `RANGE 50 KM`.
- Under 10 px from home: neither.
- While a finger turns or zooms, nothing moves under it till it lifts: the panel stays as it was (`t_panel`).

On the flat map there is no rim, so north stays up and drag anywhere zooms.

## On-screen keyboard (ui_wifi.c)

Ten keys a row, four rows, three pages, every sign a WiFi password may have. Special keys are byte codes in the
string, and a run of the same code is one wide key:

```c
enum { K_SHIFT = 1, K_RUB, K_SIGNS, K_LETTERS };
static const char KEYS[3][4][11] = {
    { "1234567890", "qwertyuiop", "asdfghjkl\x02", "\x01zxcvbnm.\x03" },
    { "1234567890", "QWERTYUIOP", "ASDFGHJKL\x02", "\x01ZXCVBNM.\x03" },
    { "!@#$%^&*()", "-_=+[]{}\\|", ":;'\",./?`\x02", "\x04<>~      " },
};
#define KB_CW (GFX_W / 10)                             /* a key's width */
#define KB_RH ((BAR_Y - 4 - KB_Y0) / 4)                /* a row's height */
```

- Password: dots, the last letter shows 1.2 s, SHOW for all of it, CANCEL forgets it, a long BOOT wipes it.
- One capital, then small letters again. DEL repeats while held. The field shows the end of the text, where the
  typing is.
- The same keyboard types the home (`FIND`) and the watch list (letters come out as capitals). One `kb_mode`.
- Keys on square touch boards only: `UI_WIFI_PAGE (RADAR_HAS_TOUCH && !RADAR_ROUND && !RADAR_WEB)`. In the web page
  twin the board has the WiFi, so no keyboard there.
- A key cell is 32 x 35 px on 320x240, 48 x 53 on 480x320, drawn 4 px smaller.

## SET: tabs, rows, cards

One row a setting, what it does under its name:

```c
static const struct { const char *label, *desc; int kind, tab; } ROWS[S_ROWS] = {
    { "RANGE",     "or drag inside the scope",   K_NUM, T_VIEW },
    { "ON GROUND", "planes parked or taxiing",   K_BTN, T_DATA },
    { "SOS",       "7500 7600 7700 round home: red", K_BTN, T_LED },
    ...
};
```

- Kinds: `K_NUM` minus and plus with the value, repeating. `K_BTN` a button that says the value, a tap goes round.
  `K_INFO` words.
- Tabs are the bottom bar's buttons: VIEW, DATA, UNITS, RADAR, LED (where there is one), BACK last. `row_at` skips
  rows of other tabs. A tab opens from its top. Each row is 40 px, big enough for a finger. A tab that fits has no
  scroll bar. On the round glass the bar is VIEW, DATA, BACK: the RADAR and UNITS rows sit in VIEW, its bar has no
  room.
- `desc` says what a setting does, or where else it can be done (`or drag the scope's rim`).
- A live row tells how it is: `3 set, 1 flying, 0 in range`, `version 0.4, 0.5 on the site`.
- A unit row says AUTO and what AUTO is now: `AUTO KM/H`.
- Air Quality shows a palette's own four colours in its row, and `SAVED BY ITSELF` in the header.
- A dangerous setting (factory RESET in Thermo Relay) needs holding.
- A button that cannot work says why: `NO FR24 KEY: SEND IT FROM THE SITE`. Never a dead button.
- A gear under the radar's scope (bottom right, RADAR look only, square touch boards) opens SET at its RADAR tab:
  options that change the look of the instrument, all off by default (P, M on the bench boards).

## Toasts

A box in the middle of the screen for about 2 s, for what a tap just did, in words: `RANGE 50 KM`, `NORTH UP`, the
theme's name, the weather page's name. A new weather warning: a 5 s toast. On paper the toast is a framed card, and
the paper's looks and weather pages show none: their name is in the key strip.

## Screens that take turns by themselves

Flight Radar 0.17, SET, VIEW, WEATHER FOR and LOOK FOR (an owner: weather for a minute, then radar for three, "and
keeps running the cycle"). `ui_turn(u, now, busy, cmd)` runs once a frame, after the finger, BOOT and the keys:

- Off until set. The minutes are fixed steps (`ui_turn_step`), two `K_NUM` rows held at their ends.
- Anything a person does starts the count again: a finger down, a frame with BOOT or a key (`busy`), a picked
  plane, any other page, the e-paper's gathered keys, and a look changed by any hand, on the screen or from the
  site. The last is seen as `cfg.look != turn_was`, with no hook in each path that can change it. It never turns
  away from what someone is doing, and the count starts from the last held frame.
- The weather goes back to the last look that was not the weather (`turn_back`): a person's MAP stays theirs.
- No forecast yet: the look stays a whole turn more, never an empty weather screen.
- A turn is an ordinary look change (`cmd->cfg`): kept, told to the site, whose twin follows; the weather comes on
  its dial; the e-paper is printed whole. The site's twin runs no turns of its own: two timers would fight over the
  board's look.
- Tested on the PC frame by frame, 100 ms a frame: the turns at their minutes, a touch at 2:30 moving the turn to
  5:30, each hold, the clock's wrap after 49.7 days (P).

## A light out of sight, on the screen

Flight Radar 0.17, SET, LED, ON SCREEN (an owner: the CYD's LED is on the back, "use the color of the text of the
current look"). Whatever lights the LED lights the look's button too, on every board, the ones with no LED as well:

- One source: `light_shown(l, cfg, now)` (core/light.c) gives the event the LED shows, in the lit part of its blink,
  or -1. The firmware hands it to `ui_alarm(u, ev)` each frame, which keeps `ui_t.alarm` (event + 1, 0 none). A byte
  in `ui_t` is enough to redraw: the pages' whole-`ui_t` compare and the background's own pace see it change.
- Letters in the event's colour, as asked first, vanish where the theme is that colour already: green on PHOSPHOR,
  red on CRIMSON, amber on AMBER, cyan on ARCTIC. So the face fills with the colour and the name is dark (`C_BG0`).
  Measured on the PC, dark letters on each face: 5.4 (red) to 17.9 (white). The LED's pure blue (0, 0, 255) is
  2.3 behind dark letters; the screen's is (90, 150, 255), 6.7. `light_screen_rgb` has the screen's colours.
- The light map (paper) keeps ink letters; its white is the paper itself, so white turns the face to ink and the
  letters to paper.
- The round glass shows its look's name only with its menu: the rim lights instead, a 4 px band at the edge outside
  the disc. That edge is dark on every look, the light map's too, so the paper rule is not used there: with it the
  first build drew an ink band on the dark rim, gone [seen in the preview].
- A blink shorter than 400 ms is 400 ms on the screen, and so is the dark part (the LED's SOS is 120 of 250): the
  big RGB boards draw their background a few times a second and would show a short one at random or not at all.
  The LED's steady amber stays steady. TEST stays the LED's alone.
- A finger on the button draws it pressed, not lit: the touch puts the light out anyway (`light_touch`).
- The boards with no LED get the LED's rows on a tab of their own, ALARM, without BRIGHTNESS, TEST and RED ON; the
  round glass under DATA (no room for a third tab). Paper shows none: it cannot blink. The site's twin shows none: it
  has no events of its own. `UI_SCREEN_ALARM` and `UI_EVENTS` in ui_int.h say which.
- ON SCREEN is kept by itself (NVS `"ledscreen"`, one byte, on at first), not as a new version of the LED's blob:
  a board flashed back reads a newer blob as the defaults and would lose its LED settings.
- Checked in the PC preview on six screens: every shot the same as before but the SET pages and the new ones, the
  e-paper's all the same; the button in each event's colour, white on the four looks, green on the five themes.

## BOOT and other buttons

Where a board has a BOOT the firmware can read (`RADAR_HAS_BUTTON`), it is a control too (the button's driver:
[buttons-keys.md](../../esp32-lcd-touch/references/buttons-keys.md)):

- short in the first seconds: the next screen revision; held: skip the alignment crosses (first-run-flow.md);
- short in WEATHER: the next page, on every board. On the RPi LCD (no touch) it is the only way;
- a long BOOT wipes a typed password.

Boards whose BOOT is a screen line (the plain CrowPanel 5.0 and 7.0, the Waveshare 7 and 7B) or whose buttons go to
another chip (Tab5) have none: the pages do not say "PRESS BOOT" there.

## Traps of touch

- **One touch read a frame (M).** At 8 fps a swipe gave one or two points and came out as a tap: only radar and target
  ever worked. Read between the strips too, every 15 ms, queue the samples with their times, feed `ui_touch` at the
  start of the next frame.
- **Counting samples instead of time (M).** Alignment needed 5 samples: at 12 fps over half a second, normal presses
  were ignored. A press counts after 0.3 s, time-based, 2 samples at least.
- **Resistive first readings wander (M).** Taps landed on the neighbour, a tap read as a drag. Pressure first (Z1, Z2,
  `Z_MIN 350`): a light press is nothing, else the panel invents taps. 5 reads per axis, median. Start point the
  median of the first 3 samples, hit decided again there. A tap's place the median without the last sample.
- **A phantom tap at power-up (M).** A 3.5" sometimes came up one look on after a restart, as from a tap on LOOK: one
  noisy XPT2046 read became a press, and any press with a release is a tap. A press now counts after two reads in a
  row, and nothing is read for 1.5 s after start; a press held from the start counts only after a lift. 10 of 10
  restarts kept the look (driver side: touch/xpt2046.md).
- **Three points with plain means (M).** One sloppy press ruined the alignment. 5 crosses, skip the first 120 ms of
  each press, median, least squares over all, refuse if the worst miss is over 18 px. For a panel that bends: 9
  crosses and a quadratic map, refuse over 14 px, worst cross 8 px measured, plus a sanity check of the saved numbers
  at load.
- **The axes (M).** On the 2.8" CYD the touch axes are swapped and both run backwards. Without alignment only the
  biggest button could be hit. The affine fit sorts out any swap and mirror for every chip, so no driver needs flags.
  The round board goes through the crosses too, for the same reason.
- **No way back to alignment when it is wrong (M).** If touch is off no button can be hit. Holding a finger still 4 s
  on nothing (WiFi Radar: anywhere) brings the crosses, and BOOT skips them. A saved alignment that looks crazy for
  the chip's raw span starts the crosses by itself.
- **The touch on the LCD bus (M).** Read it only between transfers: frame-and-speed.md.
- **Buttons over a map are hard to hit (M).** A person said so. Answer 8 to 12 px further out than drawn.
- **A palm on a dark screen (P).** The first touch on a dimmed screen changed the setting under it. The first touch
  only wakes it.
- **Held + writes flash twenty times (M).** Mark dirty, save 3 s after the last change.
- **Turning the screen breaks the touch (P).** A screen mounted upside down is one MADCTL bit, and the alignment must
  turn with it or every tap lands mirrored: `a = -a, b = -b, c = W-1-c` and the same for y, no new crosses needed.
- **Small targets next to each other (M, feedback).** On FusionEdge's screen on the 2.8" CYD, taps missed the volume
  and a tap hid the buffer indicator. Give each target room and a hit area bigger than its picture, and never put a
  target that hides something next to one that is used often.
