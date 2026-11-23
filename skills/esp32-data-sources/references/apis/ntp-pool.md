# pool.ntp.org: the time

The NTP Pool, through ESP-IDF's SNTP client. OWLOS Flight Radar asks it for UTC: the radar's CLOCK option, the weather's now and
home's clock, the sun's height for sunrise, sunset and the night light.

How sure: run on every board since 0.6. [measured]

## How it is asked

```c
void net_time_start(void)
{
    static int started;
    if (started) return;
    started = 1;
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&c);
}

uint32_t net_utc(void)
{
    time_t t = time(NULL);
    return t > 1700000000 ? (uint32_t)t : 0;          // not set yet, it counts from 1970
}
```

- Started once, as soon as WiFi is up, whether or not a clock is drawn (since 0.15, from bertvm; in 0.6 only while the CLOCK
  option was on). Then it keeps itself right.
- `net_utc()` is 0 until the time came: the screen draws no clock, the night light takes it as day.
- No time zone anywhere on the board: UTC for the radar's clock, Open-Meteo's `utc_offset_seconds` for home's clock, the sun
  worked out from UTC and the point.

## HTTPS does not wait for it

The 5.5 builds do not check certificate dates (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off). A contributor's gate that held every HTTPS
request until the clock was set was left out: a network that blocks NTP would then never fetch planes, tiles or weather.
[reasoned]

## Failures met

- On the 3.5" the map stopped coming until a restart; SNTP was the first suspect. With the clock on and off the map came the same:
  not the cause. [measured]
