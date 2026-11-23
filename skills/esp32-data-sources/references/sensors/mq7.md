# MQ7: carbon monoxide gas sensor

A heated tin-oxide sensor made for carbon monoxide; its output voltage rises with gas. On the OWLOS Air Quality board on the
ADS1015's AIN2 (`ads1015.md`). From `code/aq_sensor_hw.c` and `core/aq.c`; the board in `../air-quality.md`.

How sure: the voltage read on all boards; why it is so low is open. [measured]

## What was found

- About 0.15 V, low, unexplained. [measured]
- No ppm without a calibration gas, so none are shown. It is not a CO alarm.

## How it is shown

The same way as the MQ135 (`mq135.md`): the ratio to the lowest minute of the last 24 hours after a 3 minute warm-up, GOOD under
1.3, FAIR under 1.8, POOR under 3.0, else BAD. It counts in `air`, the worst of the noses that can be trusted now.

## Power

Its heater is one of the two suspected of starving the supply. Brown-outs are counted, cause open.

## Traps

- Voltages near zero: unexplained. No ppm, a ratio to the day's cleanest minute. [measured, cause open]
