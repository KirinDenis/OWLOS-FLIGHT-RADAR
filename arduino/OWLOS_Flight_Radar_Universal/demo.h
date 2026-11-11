/*
 * demo - a made-up sky, for the screen before the site sent any settings and for the PC preview.
 * it writes the same JSON the free networks send (ADS-B Exchange v2), so what it shows went
 * through the real parser. every flight is invented: callsigns OWL.., registrations OW-...
 * fixed tracks at real speeds; a plane that leaves comes back in at the edge, heading in.
 */
#pragma once
#include <stdint.h>

#define DEMO_N 12

typedef struct {
    float    x, y;               /* km east and north of home */
    float    track, gs;          /* degrees, knots */
    float    alt, vrate;         /* feet, feet a minute */
    uint8_t  ground;
} demo_plane_t;

typedef struct {
    demo_plane_t p[DEMO_N];
    uint32_t     t;              /* ms of the last step */
    uint32_t     rng;
    int          emergency;      /* one plane squawks 7700 (preview only) */
} demo_t;

void demo_init(demo_t *d, float range_km, uint32_t seed);
/* moves the planes to `now` and writes the answer a free network would give for them */
int  demo_json(demo_t *d, float home_lat, float home_lon, float range_km, uint32_t now, char *out, int cap);
