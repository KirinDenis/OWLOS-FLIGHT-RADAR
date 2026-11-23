# OWLOS Air Quality: the board, its bus and its model

The air monitor: what is on its bus, how the bus is driven so a sensor can never hang the board, how a reading becomes a value with
a quality, the history, the barometer forecast, and what can be told about power. Each sensor's own start and read is in
`sensors/<sensor>.md`.

From OWLOS-AIR-QUALITY-UI: `firmware/main/sensor_hw.c`, `i2c_bus.c`, `dht22.c`, `core/aq.c`, `aq.h`, `sensors.h`, `core/ui_meteo.c`.
The first three are whole in `code/` (`aq_sensor_hw.c`, `aq_i2c_bus.c`, `aq_dht22.c`). Hardware: the OWLOS Air Quality board,
ESP32-D0WDQ6 rev 1.0, 4 MB flash, no PSRAM, a 3.5" ILI9486 panel, 7 boards exist. Firmware on ESP-IDF 5.5, no network at all.
Measured facts are from 2026-10-22. Tags as in SKILL.md.

## 1. The bus

- CCS811 at I2C 0x5A: eCO2 ppm, TVOC ppb. Works only when STATUS is read first. [measured] `sensors/ccs811.md`
- An ADS at I2C 0x48, 4 ADC channels: an ADS1015 (12 bit), not the 1115 of the old notes. [measured] `sensors/ads1015.md`
- MQ135 on the ADS's AIN1: a voltage, about 0.1 V, low, unexplained. [measured] `sensors/mq135.md`
- MQ7 on AIN2: about 0.15 V, low, unexplained. [measured] `sensors/mq7.md`
- A photoresistor on AIN3: 0.9 to 3 V. [measured] `sensors/photoresistor.md`
- HDC1080 at I2C 0x40: C and %RH. TI's ids, not the SI7021 the old README said. About 4.5 C hotter than the DHT22, probably board
  heat. [measured] `sensors/hdc1080.md`
- DHT22 on GPIO 32, its own wire: C and %RH. Works. [measured] `sensors/dht22.md`
- BMP280 at I2C 0x76 or 0x77: hPa and C. SILENT at every address on all 7 boards. [measured] `sensors/bmp280.md`

I2C on SDA 21, SCL 22, about 90 kHz. The board file is the truth: I2C autoscan was tried once and rejected, and there are no
runtime pins. A sensor is asked at its known address.

Each sensor is a state the screen shows: ABSENT, STARTING, OK, ERROR. Three failed reads in a row make it ERROR (`FAILS_MAX`). A
missing or failed one is asked again every 10 s, so a loose wire that comes back comes back on screen too.

## 2. I2C by hand

ESP-IDF 5.5's `i2c_master` spins forever after a NACK on the classic ESP32: it waits for a free bus with no time limit
(`s_i2c_send_commands` in i2c_master.c). The chip at 0x40 put it there. The task watchdog restarted the board, and the same chip
hung it again: a restart loop. [measured]

So the bus is bit-banged (`code/aq_i2c_bus.c`): open drain, every wait has a limit, a NACK is just an error.

- Half clock 5 us. `STRETCH_US 25000`: the CCS811 holds SCL low while it thinks, this is the most it may.
- SCL is let go and then waited for. An interrupt in the middle only makes a clock longer, the master owns the clock, so nothing
  needs interrupts off.
- A slave that holds SDA gets nine clocks and a stop (`i2c_recover`), also at bus start: a slave cut off mid byte by the last
  reset may still hold it.

And a guard, for a chip that still hangs something. Which sensor is being talked to goes into RTC memory first. RTC memory
survives a restart, not a power cut:

```c
#define GUARD_MAGIC 0x41514755u
static RTC_NOINIT_ATTR uint32_t r_magic, r_in, r_bad;

static void guard(int i) { r_in = (uint32_t)(i + 1); }
static void unguard(void) { r_in = 0; }

static void guard_boot(sens_t *s)
{
    esp_reset_reason_t why = esp_reset_reason();
    int crash = why == ESP_RST_TASK_WDT || why == ESP_RST_INT_WDT || why == ESP_RST_WDT || why == ESP_RST_PANIC;
    if (r_magic != GUARD_MAGIC || why == ESP_RST_POWERON) {
        r_magic = GUARD_MAGIC;
        r_bad = 0;
    } else if (crash && r_in >= 1 && r_in <= SENSORS) {
        r_bad |= 1u << (r_in - 1);
    }
    r_in = 0;
    /* each one in r_bad: SS_ERROR, "HUNG I2C, OFF TILL POWER" on the screen, left alone */
}
```

The same `RTC_NOINIT_ATTR` idea counts restarts and brown-outs since power on, for the screen.

## 3. From a reading to a value

The model never sees hardware: once a second `aq_tick` gets a `sens_t`. The same code runs in the preview and the demo.

- A `have_*` says this second brought something new.
- Value: the median of the last 3 readings, so one wild DHT22 read does not move the screen. The light: 5, since a lamp flickers
  and one read lands anywhere on the flicker; then an average with 0.25 on top.
- Every channel has a quality: Q_NONE no sensor, Q_WARM, Q_OK, Q_LOST (older than 15 s, not "now").
- A level only for Q_OK. `air` is the worst of the noses that can be trusted now: eCO2, TVOC, MQ135, MQ7.
- Trend: least squares over 10 minutes of samples 10 s apart, per hour, needs 12 samples over 3 minutes.

Levels, GOOD under / FAIR under / POOR under:

- eCO2 ppm: 800 / 1200 / 2000. The usual ventilation advice.
- TVOC ppb: 220 / 660 / 2200. The bands sensor makers print for indoor air.
- Temperature C: GOOD 19 to 25, FAIR within 3 outside it, POOR within 6. A comfort band.
- Humidity %: GOOD 40 to 60, FAIR within 10 outside it, POOR within 20. A comfort band.
- MQ ratio to the baseline: 1.3 / 1.8 / 3.0.

Dew point: `a = 17.62`, `b = 243.12`, `g = ln(rh / 100) + a t / (b + t)`, `dew = b g / (a - g)`.

Temperature and humidity come from one source, chosen in the settings (`th_src`), the other when it is missing. DHT22 first: the
HDC1080 sits in the board's heat. `t_off` and `rh_off` are the user's correction.

## 4. The history

- A point a minute for 24 h (1440), a point every 10 minutes for 7 days (1008), 8 channels as int16 scaled per channel, plus
  flags. About 44 KB.
- In its own flash partition "aqhist", 512 KB, a ring of 10 slots of 48 KB, a save every 10 minutes: about 14 erases a slot a day,
  rated 100k, about twenty years. Data first, header last.
- Demo numbers are never saved.
- The board has no clock. History counts back from now, and a power cut is a mark on the graph (`HF_BOOT`) whose length nobody
  knows. It is not invented.

## 5. Pressure and the Zambretti forecast

Built for one purpose: a fast pressure change is what the user wants to see first. So the METEO page leads with how fast it moves,
the forecast comes second. Units: mmHg by default, hPa by switch. `mmHg = hPa * 0.750062`.

The change over 3 hours is the meteorologists' pressure tendency. From the minute history:

```c
/* the pressure `age` minutes back: the mean of the minutes around it. 0 if those minutes are
 * not there, or a power cut lies between (then nobody knows how long ago it really was) */
static int pres_back(const aq_hist_t *h, int age, float *out);

p->slp = now / powf(1.0f - alt_m / 44330.0f, 5.255f);     // to sea level, alt_m is a setting
if (pres_back(h, 60, &back)) p->d1h = now - back;
// 3 hours, or at least 2 scaled to 3
for (int age = 180; age >= 120; age -= 10)
    if (pres_back(h, age, &back)) { p->d3h = (now - back) * 180.0f / (float)age; p->have = 1; break; }
if (!p->have) return;
p->trend = p->d3h > 1.6f ? 1 : p->d3h < -1.6f ? -1 : 0;     // the steady band of the usual Zambretti code
p->zambretti = zambretti(p->slp, p->trend);
float a3 = fabsf(p->d3h), a1 = have1 ? fabsf(p->d1h) : 0.0f;
p->alarm = a3 >= alert * 2.0f ? 2 : (a3 >= alert || a1 >= alert * 0.5f) ? 1 : 0;   // alert: 3 hPa in 3 h by default
```

The Zambretti forecaster the way beteljuice.com wrote it down: the barometer's range cut in 22 steps from 950 to 1050 hPa, each
step and the trend give one of 26 forecasts. No season and no wind here: the board has neither a calendar nor a vane.

```c
static const char *const ZTEXT[26] = {
    "Settled fine", "Fine weather", "Becoming fine", "Fine, becoming less settled", "Fine, possible showers",
    "Fairly fine, improving", "Fairly fine, possible showers early", "Fairly fine, showery later",
    "Showery early, improving", "Changeable, mending", "Fairly fine, showers likely", "Rather unsettled, clearing later",
    "Unsettled, probably improving", "Showery, bright intervals", "Showery, becoming less settled",
    "Changeable, some rain", "Unsettled, short fine intervals", "Unsettled, rain later", "Unsettled, some rain",
    "Mostly very unsettled", "Occasional rain, worsening", "Rain at times, very unsettled",
    "Rain at frequent intervals", "Rain, very unsettled", "Stormy, may improve", "Stormy, much rain",
};
static const uint8_t Z_RISE[22]   = { 25, 25, 25, 24, 24, 19, 16, 12, 11, 9, 8, 6, 5, 2, 1, 1, 0, 0, 0, 0, 0, 0 };
static const uint8_t Z_STEADY[22] = { 25, 25, 25, 25, 25, 25, 23, 23, 22, 18, 15, 13, 10, 4, 1, 1, 0, 0, 0, 0, 0, 0 };
static const uint8_t Z_FALL[22]   = { 25, 25, 25, 25, 25, 25, 25, 25, 23, 23, 21, 20, 17, 14, 7, 3, 1, 1, 1, 0, 0, 0 };

static int zambretti(float slp, int trend)
{
    const float top = 1050.0f, bottom = 950.0f, step = (top - bottom) / 22.0f;
    int o = (int)floorf((slp - bottom) / step);
    if (o < 0) o = 0;
    if (o > 21) o = 21;
    return trend > 0 ? Z_RISE[o] : trend < 0 ? Z_FALL[o] : Z_STEADY[o];
}

int zambretti_icon(int z)     // 0 sun, 1 sun and cloud, 2 cloud, 3 rain, 4 storm
{
    if (z < 0) return -1;
    return z <= 2 ? 0 : z <= 8 ? 1 : z <= 15 ? 2 : z <= 23 ? 3 : 4;
}
```

It needs at least 2 hours of history with no power cut inside; until then the page says "Needs 3 hours of pressure". On screen it is
"BAROMETER FORECAST, 12 H": a barometer forecast, not a weather service.

All of this has run only on the made-up pressure of `core/sim.c` and on the preview, since no BMP280 ever answered. [PC test] Say
so.

## 6. What can be told about power

None of these is a voltmeter on the 5 V rail, each is a clue, and they are on the sensors page:

- The XPT2046 touch chip's VBAT input reads 0 V: it is unwired on this board. [measured]
- The ADS1015 is not wired to the supply, it is no voltmeter (AIN0 is left unused). [measured]
- HDC1080 config bit 11: its supply under 2.8 V.
- CCS811 RAW_DATA: the current through its layer, the voltage on it.
- Brown-out restarts, counted in RTC memory, since power on. The MQ heaters are suspected of starving the supply; not proven.
  [counted, cause open]

The backlight is hard-wired to power on this panel, so no dimming by PWM: the night palette does the dimming, driven by the light
sensor (`sensors/photoresistor.md`).

## Traps

- Watchdog restart loop: `i2c_master` waits forever after a NACK on the classic ESP32, the chip at 0x40. Bit-banged I2C, every wait
  with a limit, and the RTC guard. [measured]
- The README's chip does not answer its way: 0x40 is an HDC1080, 0x48 an ADS1015. Read the id registers, never trust the old notes.
  [measured]
- One wild value on the graph: a DHT22 glitch, lamp flicker on the photoresistor. Median of 3, of 5 and a slow average for light.
  [measured]
- A false 3-hour pressure trend: a power cut inside the window. Refuse across `HF_BOOT`. [PC test]
- Hidden restarts: brown-outs, the MQ heaters suspected. Count them in RTC memory, show them. [counted, cause open]
