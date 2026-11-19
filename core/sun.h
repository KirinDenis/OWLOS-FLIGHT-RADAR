/* sun - how high the sun stands over a point, from the time alone: no network, no time zone. the
 * screen's night light (SET's NIGHT) goes by it, and day and night anywhere else can */
#pragma once
#include <stdint.h>

/* degrees above the horizon at lat, lon (degrees, east and north positive) at utc (seconds since
 * 1970). below 0 the sun is down: -0.8 its upper edge sets, -6 the end of civil twilight. the low
 * precision formula of the Astronomical Almanac: good to a hundredth of a degree for decades */
float sun_elevation(float lat, float lon, uint32_t utc);
