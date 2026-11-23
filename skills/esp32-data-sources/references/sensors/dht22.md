# DHT22 (AM2302): temperature and humidity on one wire

On the OWLOS Air Quality board on GPIO 32, its own wire. The first choice for temperature and humidity there: the HDC1080 sits in
the board's heat. From `code/aq_dht22.c`; the board in `../air-quality.md`.

How sure: works on the board. [measured] The RMT advice is not tried.

## The read

Line low 1.2 ms, let go; its answer 80 us low, 80 us high, then 40 bits: 50 us low and 26 us high for a 0, 70 us high for a 1.

- Timed off the CPU cycle counter (`esp_cpu_get_cycle_count`), threshold 45 us.
- Interrupts are held off on this core for about 5 ms.
- Read every 3 s (it wants 2 s at least).
- A checksum and a range check.
- The value is the median of the last 3 readings: one wild read does not move the screen.

## With WiFi

In a firmware with WiFi on the same core a 5 ms critical section is a real cost: this firmware has no WiFi. For a board that has
both, use the RMT peripheral instead. [not tried]

## Traps

- One wild value on the graph: a DHT22 glitch. Median of 3. [measured]
