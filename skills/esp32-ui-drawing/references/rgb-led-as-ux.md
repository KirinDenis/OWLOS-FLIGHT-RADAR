# The RGB LED as UX (core/light.c)

From across the room a person cannot read the screen. The LED says one thing about what happened. Portable logic
in `core/`, the pins in `firmware/main/led.c`. Full code: [code-light.md](code-light.md) (an older copy; the
WEATHER event below is newer). Pins and LED wiring per board:
[leds.md](../../esp32-lcd-touch/references/leds.md) and the board files in
[esp32-boards](../../esp32-boards/SKILL.md).

How sure: (M) seen on a real board, (P) built, read in the code, (R) reasoned.

## The events and their looks

```c
enum { LIGHT_SOS, LIGHT_RANGE, LIGHT_UP, LIGHT_DOWN, LIGHT_NEAR, LIGHT_NODATA, LIGHT_WEATHER, LIGHT_EVENTS };
static const struct { uint8_t r, g, b; uint16_t on_ms, period_ms; } LOOK[LIGHT_EVENTS] = {
    [LIGHT_SOS]    = { 255, 0, 0, 120, 250 },        // red, fast
    [LIGHT_RANGE]  = { 0, 255, 0, 700, 1400 },       // green
    [LIGHT_UP]     = { 0, 0, 255, 700, 1400 },       // blue
    [LIGHT_DOWN]   = { 0, 255, 255, 700, 1400 },     // cyan
    [LIGHT_NEAR]   = { 255, 255, 255, 300, 2000 },   // white, a short blink now and then
    [LIGHT_NODATA] = { 255, 80, 0, 1000, 1000 },     // amber, steady
    [LIGHT_WEATHER] = { 255, 170, 0, 1000, 2000 },   // yellow, slow: no data's amber does not blink
};
/* which wins when several are lit */
static const uint8_t ORDER[LIGHT_EVENTS] = { LIGHT_SOS, LIGHT_WEATHER, LIGHT_RANGE, LIGHT_UP, LIGHT_DOWN, LIGHT_NEAR, LIGHT_NODATA };
```

The weather warning event is newer: on by default, second in the order after SOS, its settings blob went to version
4 and older versions are read over the defaults (P; the bench CYD kept its own choices after the update, M).

## Rules

- The blink carries the urgency. An emergency blinks fast, a state does not blink at all.
- A priority order. Each event on or off by the user.
- Moments and states differ: take-off is a moment, 10 s. In range is a state.
- HOW LONG: till touched (a minute at most), 10 s, or while it lasts. Any touch or BOOT puts it out: what was seen
  goes out.
- A touch sample is not a finger: a sample comes every frame, so look at its `down`. Flight Radar put the LED out on
  "a sample came", and from 0.3 to 0.4 no event ever lit it (M). TEST takes another path: a working TEST proves the
  pins, not the events. To check an event without seeing the LED, log each change from dark to lit, on two boards
  with one home, one with the fix and one without (M: the fixed one lit 31 times, the other 0, no false finger in
  3057 frames).
- Brightness 12, 64, 255: the CYD's LED is bright, low is for the night.
- Defaults off for what would be always on: `ANY NEAR` next to an airport is on all day, `NO DATA` on a fresh board.
- How close is near: 1, 2, 5 or 10 of the distance unit.
- A TEST row: red, green, blue, 1.5 s each. On boards where nothing tells which pin the red is on (the CYDs: GPIO 4
  or 22), a row picks it (`RED ON`).
- Thermo Relay: red heating, blue cooling, green off and well, yellow resting, **red fast blink** = alarm, twice a
  second, so it can't be taken for the steady red of heating. Power on shows red, green, blue while the screen says
  which colour should glow on which pin.

## Driver facts

On the CYD each colour is pulled to ground to light, so LEDC outputs are inverted (`output_invert`), its own timer so
the backlight's stays alone. WS2812 by the RMT, GRB, built only (P).

An agent cannot see an LED (M): ask a person yes or no, one pin held low for minutes. A blink-count scan was too hard
to read.
