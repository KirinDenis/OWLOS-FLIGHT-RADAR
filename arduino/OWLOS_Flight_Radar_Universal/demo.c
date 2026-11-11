#include "demo.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define D2R (3.14159265f / 180.0f)

/* who flies in the demo: callsign, registration, type, category, height (ft), climb (ft/min),
 * speed (kt). all invented */
static const struct { const char *flight, *reg, *type, *cat; float alt, vrate, gs; } FLEET[DEMO_N] = {
    { "OWL101", "OW-AAL", "A320", "A3", 36000, 0, 452 },
    { "OWL205", "OW-ABM", "B738", "A3", 6500, 2200, 285 },
    { "OWL310", "OW-ACN", "A21N", "A3", 8200, -900, 240 },
    { "OWL77H", "OW-HVY", "B77W", "A5", 39000, 0, 492 },
    { "OWL412", "OW-ADE", "E190", "A3", 24000, 0, 402 },
    { "OWLC17", "OW-LGT", "C172", "A1", 3500, 0, 105 },
    { "OWLR44", "OW-HEL", "R44", "A7", 1200, 0, 90 },
    { "OWLG21", "OW-GLD", "GLID", "B1", 4600, 0, 55 },
    { "OWL9J", "OW-BIZ", "C56X", "A2", 28000, 0, 425 },
    { "OWL501", "OW-AFG", "A319", "A3", 0, 0, 14 },
    { "OWL620", "OW-AGH", "B38M", "A3", 34000, 0, 461 },
    { "OWL733", "OW-AHI", "A333", "A5", 37000, 0, 478 },
};

static float rnd(demo_t *d)
{
    d->rng = d->rng * 1664525u + 1013904223u;
    return (float)(d->rng >> 8) / 16777216.0f;
}

// a new place for plane i: anywhere inside the range at the start, at the edge heading in later
static void spawn(demo_t *d, int i, float range, int edge)
{
    demo_plane_t *p = &d->p[i];
    float a = rnd(d) * 360.0f;
    float r = edge ? range * 1.3f : range * (0.15f + 0.8f * rnd(d));
    p->x = r * sinf(a * D2R);
    p->y = r * cosf(a * D2R);
    p->track = edge ? fmodf(a + 180.0f + (rnd(d) - 0.5f) * 70.0f + 360.0f, 360.0f) : rnd(d) * 360.0f;
    p->alt = FLEET[i].alt;
    p->vrate = FLEET[i].vrate;
    p->gs = FLEET[i].gs;
    p->ground = FLEET[i].alt <= 0;
    if (p->ground) {                                   // taxiing near the field, 12 km out
        p->x = 9.0f + rnd(d);
        p->y = -8.0f + rnd(d);
    }
}

void demo_init(demo_t *d, float range_km, uint32_t seed)
{
    memset(d, 0, sizeof *d);
    d->rng = seed ? seed : 1;
    for (int i = 0; i < DEMO_N; i++) spawn(d, i, range_km, 0);
}

static void step(demo_t *d, float range, float dt)
{
    for (int i = 0; i < DEMO_N; i++) {
        demo_plane_t *p = &d->p[i];
        float km = p->gs * 1.852f / 3600.0f * dt;
        p->x += km * sinf(p->track * D2R);
        p->y += km * cosf(p->track * D2R);
        if (!p->ground) {
            p->alt += p->vrate * dt / 60.0f;
            if (p->vrate > 0 && p->alt > 33000) { p->alt = 33000; p->vrate = 0; }       // levels off
            if (p->vrate < 0 && p->alt < 2500) { p->alt = 2500; p->vrate = 0; }
        } else if (fabsf(p->x - 9.5f) > 2.0f || fabsf(p->y + 7.5f) > 2.0f) {
            p->track = fmodf(p->track + 180.0f, 360.0f);                              // stays on the field
        }
        if (sqrtf(p->x * p->x + p->y * p->y) > range * 1.5f) spawn(d, i, range, 1);
    }
}

int demo_json(demo_t *d, float home_lat, float home_lon, float range_km, uint32_t now, char *out, int cap)
{
    if (d->t) {
        float dt = (float)(now - d->t) / 1000.0f;
        while (dt > 0) {                               // in steps, so a long gap still curves right
            float s = dt > 5.0f ? 5.0f : dt;
            step(d, range_km, s);
            dt -= s;
        }
    }
    d->t = now;
    int n = snprintf(out, (size_t)cap, "{\"ac\":[");
    float coslat = cosf(home_lat * D2R);
    for (int i = 0; i < DEMO_N && n < cap - 400; i++) {
        const demo_plane_t *p = &d->p[i];
        float lat = home_lat + p->y / 110.574f, lon = home_lon + p->x / (111.320f * coslat);
        char alt[16];
        if (p->ground) snprintf(alt, sizeof alt, "\"ground\"");
        else snprintf(alt, sizeof alt, "%d", (int)p->alt);
        int sq = d->emergency && i == 4 ? 7700 : 1000 + i * 111;
        // what a plane says of itself: where the autopilot takes it, the speed through the air, the wind
        int set = ((int)p->alt + (p->vrate > 200 ? 4000 : p->vrate < -200 ? -4000 : 0) + 500) / 1000 * 1000;
        n += snprintf(out + n, (size_t)(cap - n),
                      "%s{\"hex\":\"%06x\",\"flight\":\"%-8s\",\"r\":\"%s\",\"t\":\"%s\",\"category\":\"%s\","
                      "\"lat\":%.5f,\"lon\":%.5f,\"alt_baro\":%s,\"gs\":%.1f,\"track\":%.1f,\"baro_rate\":%d,"
                      "\"squawk\":\"%04d\",\"seen_pos\":0.4",
                      i ? "," : "", 0xA0B000 + i, FLEET[i].flight, FLEET[i].reg, FLEET[i].type, FLEET[i].cat,
                      (double)lat, (double)lon, alt, (double)p->gs, (double)p->track, (int)p->vrate, sq);
        if (!p->ground)
            n += snprintf(out + n, (size_t)(cap - n),
                          ",\"nav_altitude_mcp\":%d,\"nav_heading\":%.0f,\"ias\":%.0f,\"mach\":%.3f,\"wd\":240,\"ws\":35",
                          set < 1000 ? 1000 : set, (double)p->track, (double)p->gs * 0.62, (double)p->gs / 620.0);
        n += snprintf(out + n, (size_t)(cap - n), "}");
    }
    n += snprintf(out + n, (size_t)(cap - n), "],\"total\":%d}", DEMO_N);
    return n;
}
