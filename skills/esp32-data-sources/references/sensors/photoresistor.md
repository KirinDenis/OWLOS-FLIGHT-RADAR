# Photoresistor: the light in the room

A light-dependent resistor in a divider. On the OWLOS Air Quality board on the ADS1015's AIN3 (`ads1015.md`). From
`code/aq_sensor_hw.c` and `core/aq.c`; the board in `../air-quality.md`.

How sure: read on the boards. [measured]

## What was found

- 0.9 to 3 V across the room's light. [measured]
- Light in percent: `v / 3.3 * 100`, inverted by a setting: the divider may read lower in brighter light.

## Reading it steadily

A lamp flickers, and one read lands anywhere on the flicker. So the median of the last 5 readings, then an average with 0.25 on
top. [measured]

## What it drives

The panel's backlight is hard-wired to power, so no PWM dimming: the night palette does the dimming. Under 6 % the screen goes
dark, over 14 % it comes back, after 8 s and 3 s of patience: a dead band so it does not flip.

## Traps

- One wild value on the graph: lamp flicker. Median of 5 and a slow average. [measured]
