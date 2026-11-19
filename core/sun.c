// the sun's height over a point, see sun.h
#include "sun.h"
#include <math.h>

#define RAD (3.14159265358979 / 180.0)

float sun_elevation(float lat, float lon, uint32_t utc)
{
    const double d = ((double)utc - 946728000.0) / 86400.0;   // days since 2000-01-01 12:00 UTC (J2000)
    // where the sun is on the ecliptic: its mean longitude, its mean anomaly, the two corrections
    const double mean = fmod(280.460 + 0.9856474 * d, 360.0);
    const double anom = fmod(357.528 + 0.9856003 * d, 360.0) * RAD;
    const double ecl = (mean + 1.915 * sin(anom) + 0.020 * sin(2.0 * anom)) * RAD;
    const double tilt = (23.439 - 0.0000004 * d) * RAD;         // the earth's axis to the ecliptic
    // on the sky: right ascension and declination
    const double ra = atan2(cos(tilt) * sin(ecl), cos(ecl));
    const double dec = asin(sin(tilt) * sin(ecl));
    // how far round the earth has turned under it: sidereal time at Greenwich, then here
    const double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
    const double hour = (gmst + lon) * RAD - ra;
    const double phi = lat * RAD;
    return (float)(asin(sin(phi) * sin(dec) + cos(phi) * cos(dec) * cos(hour)) / RAD);
}
