# OWLOS Thermo Relay: the reference

A thermostat on the 2.8" CYD (ESP32-2432S028R): DS18B20 probe, a relay, touch screen. From OWLOS-THERMO-RELAY: `core/thermo.c`,
`core/thermo.h`, `core/sim.c`, `firmware/main/main.c`, `firmware/main/relay.c`, `firmware/main/ds18b20.c`, README. The control
code is whole in `code/` (`thermo.c`, `thermo.h`, `thermo_relay.c`, `thermo_ds18b20.c`). The probe itself: `sensors/ds18b20.md`.

How sure: built for the CYD, the rules were run on the PC against a simulated room (`tools/preview`). The notes do not record a
long run with a real probe and a real load, so treat its field behaviour as [built]. The rules below are read from the code
[PC test].

There is no WiFi on purpose: a thermostat that works offline cannot be broken from outside.

## 1. What it controls

One relay, one probe, one mode. Mode OFF, HEAT (heater, warm floor, boiler) or COOL (fridge, freezer, fan, cellar). Everything
inside is in C, F is only for the eye.

Wiring, all on the board's CN1 plug (check the print, revisions differ):

- GND: probe GND, relay module GND.
- IO27: DS18B20 DATA, with 4.7k from DATA to 3.3 V (most probe modules have it).
- IO22: relay module IN.
- 3.3 V: probe VDD. All three wires, parasite power is not done.

The RGB LED on the back, on = pin low: red 4, green 16, blue 17.

Relay module facts from the README:

- Many cheap modules want 5 V for the coil and switch on a LOW input. A 5 V module driven from a 3.3 V pin may never fully switch
  off: use one made for 3.3 V logic, one with a separate JD-VCC supply, or a solid state relay.
- If the relay clicks on when the screen says off, flip RELAY INPUT in SETUP.
- Mains is dangerous. A relay rated for the load, a contactor for heaters over a few kW, a qualified person for 230 V.

## 2. The safe states

The rule: nothing switches on a guess. The safe state is relay OFF, and every path falls into it.

1. Power on. `relay_init` is the first thing after the settings, before the screen and everything else. It writes the OFF level
   BEFORE turning the pin into an output, so the pin never drives the wrong way for a moment.
2. First run. Mode is OFF until a person answers what is on the relay. Nothing switches before.
3. After power on, the minimum off time runs first. `switched` is 0 and `up` starts at 0, so a compressor cut by a short power
   failure does not restart under pressure.
4. No probe: 3 failed reads in a row. One or two lost reads happen, three is a missing probe. Relay off at once.
5. Past the limit: relay off, 1 degree back inside to clear.
6. No rise: relay off, latched, a person has to press RESET ALARM.
7. Safety and OFF do not wait for the minimum on time. Only ordinary switching waits.
8. The number on the screen becomes `--.-`, never the last value.
9. The target cannot be set past the limit: `thermo_set_max` is `limit_hi - band` in HEAT, `thermo_set_min` is `limit_lo + band`
   in COOL. The screen says so when you hit it.

The module read before driving: left as an input with a weak pull-down, does something pull the relay input up? A module that
switches on LOW does, its optocoupler LED hangs from VCC. Reported by `relay_pulled_up()`, for the setup page.

```c
void relay_init(int active_low)
{
    gpio_config_t in = { .pin_bit_mask = 1ULL << PIN_RELAY, .mode = GPIO_MODE_INPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&in);
    esp_rom_delay_us(500);
    s_pulled_up = gpio_get_level(PIN_RELAY);

    gpio_set_level(PIN_RELAY, active_low ? 1 : 0);   // the level is latched before the pin turns output
    gpio_config_t c = { .pin_bit_mask = 1ULL << PIN_RELAY, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&c);
    gpio_set_level(PIN_RELAY, active_low ? 1 : 0);
    ...
}

void relay_set(int on, int active_low) { gpio_set_level(PIN_RELAY, (on ? 1 : 0) ^ (active_low ? 1 : 0)); }
```

Note the bootstrap gap this does not close: from reset until `relay_init` runs the pin is whatever the chip leaves it, and the relay
module's own pull decides. Choose a module whose idle state is off. [read from the code, not measured]

## 3. thermo_tick, once a second, in order

1. The probe. `v = probe + offset`. A step under 2 C goes through `t += 0.35 (v - t)`, so the last digit does not flicker; a bigger
   jump (offset changed, probe moved) is taken as it is. DS18B20 steps 1/16 C.
2. No-rise watch, while the relay runs.
3. Faults, the worst wins: PROBE, then NORISE, then LIMIT.
4. What the temperature asks for, `want`. In the band: whatever it asked last. That is the hysteresis.
5. The relay follows, unless the minimum time is not over. Safety and OFF do not wait.
6. Trend, every 10 s.
7. History, a point a minute.

```c
float lo = c->set - c->band * 0.5f, hi = c->set + c->band * 0.5f;
if (f || c->mode == MODE_OFF) th->want = 0;
else if (c->mode == MODE_HEAT) { if (th->t <= lo) th->want = 1; else if (th->t >= hi) th->want = 0; }
else { if (th->t >= hi) th->want = 1; else if (th->t <= lo) th->want = 0; }

uint32_t in_state = th->up - th->switched;
int next = th->relay;
th->hold_s = 0;
if (th->want != th->relay) {
    if (!th->want && (f || c->mode == MODE_OFF)) next = 0;
    else {
        uint32_t need = th->relay ? c->min_on_s : c->min_off_s;
        if (in_state >= need) next = th->want;
        else th->hold_s = (int)(need - in_state);
    }
}
```

Defaults: target 21, band 0.5, min on 30 s, min off 60 s, limit HEAT 35, limit COOL 0, no-rise 60 minutes, mode OFF.

Limits on the settings: band 0.1 to 5, min on and off up to 900 s, offset +-5, limit_hi 0 to 120, limit_lo -50 to 40, no-rise up to
480 minutes, target -40 to 110.

## 4. The no-rise alarm

It catches a probe that fell off the floor or out of the water: it reads the room, and the heater would never stop. The relay ran
`norise_min` minutes and the temperature did not move 0.5 C the right way: alarm, latched.

```c
/* progress counts from the lowest point of the run (highest when cooling), not from the
 * start: the probe lags, so the room keeps falling a few minutes after the heater comes
 * on, and an open window drops it further. from the start the heater could need more
 * than the hour to make up for that, and the alarm went off on a working heater */
if (c->mode == MODE_COOL ? th->t > th->run_t0 : th->t < th->run_t0) th->run_t0 = th->t;
float gain = c->mode == MODE_COOL ? th->run_t0 - th->t : th->t - th->run_t0;
if (gain >= 0.5f) { th->run_t0 = th->t; th->run_since = th->up; }
else if (th->up - th->run_since >= (uint32_t)c->norise_min * 60u) th->norise = 1;
```

What the screen says, what happened, the relay, how it clears:

- NO PROBE: 3 reads in a row failed. Relay off. Clears by itself when the probe answers.
- TOO HOT / TOO COLD: past the limit. Relay off. Clears by itself 1 degree back inside.
- NO RISE / NO DROP: ran the time, 0.5 C not moved. Relay off. Clears only with RESET ALARM.

## 5. The probe

The DS18B20's timing, the CRC, the all-zero trap and the 85.0 C power-on value are in `sensors/ds18b20.md`. The control task reads
what was started a second ago and starts the next, so the 750 ms conversion costs nothing.

## 6. The simulated probe

`core/sim.c`, a pretend room. Air warms from the heater, leaks outside with a 2.5 h time constant, the probe follows a minute late in
1/16 C steps with a little noise, and about once in three hours a window opens. HEAT: outside 2 to 8 C, a heater worth 30 C. COOL: a
fridge in a 21 C kitchen. OFF: a flat. It starts with a made-up 24 h, worked out in moments.

The rules around it:

- Everything else works as usual, INCLUDING the relay pin and the LED, so a relay module can be tried before the probe arrives. (The
  first note of the project said the demo never drives the relay. The code and the README say it does. The code wins: `relay_set`
  runs in both cases.)
- A real probe always wins. The simulation does not start next to one and stops when one answers: a real heater must never run on
  made-up numbers while a real reading exists.
- The screen keeps saying `SIM PROBE` and `ROOM (SIMULATED)`.
- Made-up numbers never go to flash. The real history is parked and comes back with a gap mark.

## 7. What it keeps

- 24 h, a point a minute: temperature, target in 0.01 C, seconds of that minute the relay ran, flags (starts, fault, boot, heat,
  cool, sim). `hpoint_t` is 6 bytes.
- Saved every 10 minutes (`save_in = 600`), to partition "thermo" at 0x190000, 256 KB, a ring of 12 KB slots.
- On boards that ran the radar before, "SLOT" slots lie at the same address: the magic "THRM" keeps them from being read. Touch
  alignment falls back to the radar's NVS entry when there is none.
- Settings are saved 3 s after the last touch: holding + is one flash write, not twenty.
- `HF_BOOT`: the first minute after power on. The board has no clock, so the length of the gap is not invented.
- Duty and starts an hour come from the history. They diagnose the system: many starts an hour, the band is too narrow or the probe
  too close to the heater; on near 100% and still short of the target, the heater is too small.

## 8. The tasks

- Control task, core 0, once a second: read the probe, tick, set the relay and the LED, save. Task watchdog on it.
- Screen task, core 1: reads a copy, hands back new settings. The screen never touches the relay itself.

The LED says what the words say, readable across the room. A 250 ms esp_timer does the blinking:

- Red, steady: the heater is on.
- Blue, steady: the cooler is on.
- Green: relay off, all is well.
- Yellow: it should run, the relay is resting.
- Red, fast blink: an alarm, the relay is held off.
- Dark: thermostat OFF, or the LED switched off.

At power on it shows red, green, blue for 1.5 s each while the screen names them, so a board with other pins shows itself. Full
brightness is blinding: a fifth reads well in a lit room, and green gets less since it looks brighter.

One colour, one meaning: orange only heating, blue only cooling, red only an alarm.

## Traps

- The relay chatters at the target: no hysteresis. A band, minimum on and off. [PC test]
- No-rise alarm on a working heater: progress counted from the start, the probe lags and the room still falls. Count from the lowest
  point of the run. [PC test]
- A dead line reads as 0 C: all zeros pass the DS18B20 CRC. Refuse all-zero and all-FF. [built]
- The relay clicks on when the screen says off: the module switches on LOW, or a 5 V module on a 3.3 V pin. The RELAY INPUT
  setting, a 3.3 V logic module or an SSR. [README]
- The relay flicks on at boot: the pin driven before its level is set. Set the level, then make it an output, first thing. [built]
- A heater runs on made-up numbers: a simulated probe beside a real one. A real probe always wins, the simulation stops. [built]
- Another project's data read as history: the same flash address on a reused board. A magic per project ("THRM", "SLOT", "OSMT").
  [built]
