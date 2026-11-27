# The radar scope, complete

Two generations of the same instrument. Flight Radar's map turns and works in whole numbers (the C3
has no FPU); WiFi Radar's bakes what never moves. All of it runs on hardware on the CYDs and the
round C3.

# Flight Radar, core/ui_scope.c

## Silhouettes: parts as convex polygons, turned by heading

```c
/* --- silhouettes, seen from above, nose up, in units of the size: a few convex parts each --- */

typedef struct { int8_t n; float xy[14]; } part_t;
typedef struct { const part_t *parts; int8_t np; float size; } shape_t;

static const part_t JET[] = {
    { 7, { 0, -1.0f, 0.10f, -0.82f, 0.10f, 0.80f, 0.05f, 0.98f, -0.05f, 0.98f, -0.10f, 0.80f, -0.10f, -0.82f } },
    { 4, { -0.08f, -0.22f, -0.98f, 0.26f, -0.98f, 0.38f, -0.08f, 0.12f } },
    { 4, { 0.08f, -0.22f, 0.98f, 0.26f, 0.98f, 0.38f, 0.08f, 0.12f } },
    { 4, { -0.06f, 0.64f, -0.40f, 0.88f, -0.40f, 0.98f, -0.06f, 0.86f } },
    { 4, { 0.06f, 0.64f, 0.40f, 0.88f, 0.40f, 0.98f, 0.06f, 0.86f } },
};
static const part_t BIZ[] = {
    { 6, { 0, -1.0f, 0.08f, -0.8f, 0.08f, 0.85f, 0, 0.98f, -0.08f, 0.85f, -0.08f, -0.8f } },
    { 4, { -0.07f, -0.05f, -0.80f, 0.28f, -0.80f, 0.38f, -0.07f, 0.22f } },
    { 4, { 0.07f, -0.05f, 0.80f, 0.28f, 0.80f, 0.38f, 0.07f, 0.22f } },
    { 4, { -0.08f, 0.40f, -0.22f, 0.40f, -0.22f, 0.62f, -0.08f, 0.62f } },   /* engines at the tail */
    { 4, { 0.08f, 0.40f, 0.22f, 0.40f, 0.22f, 0.62f, 0.08f, 0.62f } },
    { 4, { -0.38f, 0.86f, 0.38f, 0.86f, 0.38f, 0.96f, -0.38f, 0.96f } },      /* T-tail */
};
static const part_t LIGHT[] = {
    { 5, { 0, -0.95f, 0.10f, -0.78f, 0.08f, 0.92f, -0.08f, 0.92f, -0.10f, -0.78f } },
    { 4, { -1.0f, -0.40f, 1.0f, -0.40f, 1.0f, -0.18f, -1.0f, -0.18f } },      /* straight wing */
    { 4, { -0.38f, 0.72f, 0.38f, 0.72f, 0.38f, 0.88f, -0.38f, 0.88f } },
};
static const part_t FAST[] = {
    { 6, { 0, -1.0f, 0.09f, -0.7f, 0.09f, 0.95f, 0, 1.0f, -0.09f, 0.95f, -0.09f, -0.7f } },
    { 3, { 0.05f, -0.45f, 0.05f, 0.70f, 0.78f, 0.75f } },                     /* delta */
    { 3, { -0.05f, -0.45f, -0.05f, 0.70f, -0.78f, 0.75f } },
};
static const part_t HELI[] = {
    { 7, { 0, -0.55f, 0.22f, -0.38f, 0.24f, 0.10f, 0.10f, 0.30f, -0.10f, 0.30f, -0.24f, 0.10f, -0.22f, -0.38f } },
    { 4, { -0.05f, 0.25f, 0.05f, 0.25f, 0.04f, 1.0f, -0.04f, 1.0f } },        /* tail boom */
    { 4, { -0.20f, 0.88f, 0.20f, 0.88f, 0.20f, 0.96f, -0.20f, 0.96f } },
};
static const part_t GLIDER[] = {
    { 5, { 0, -0.85f, 0.06f, -0.7f, 0.04f, 0.95f, -0.04f, 0.95f, -0.06f, -0.7f } },
    { 4, { -1.0f, -0.32f, 1.0f, -0.32f, 1.0f, -0.20f, -1.0f, -0.20f } },
    { 4, { -0.28f, 0.84f, 0.28f, 0.84f, 0.28f, 0.92f, -0.28f, 0.92f } },
};
static const part_t DRONE[] = {
    { 4, { -0.75f, -0.65f, -0.65f, -0.75f, 0.75f, 0.65f, 0.65f, 0.75f } },
    { 4, { 0.65f, -0.75f, 0.75f, -0.65f, -0.65f, 0.75f, -0.75f, 0.65f } },
};
static const part_t GROUND[] = {
    { 4, { -0.5f, -0.8f, 0.5f, -0.8f, 0.5f, 0.8f, -0.5f, 0.8f } },
};

static const shape_t SHAPES[KINDS] = {
    [KIND_JET]     = { JET, 5, 9.0f },
    [KIND_LIGHT]   = { LIGHT, 3, 6.5f },
    [KIND_BIZJET]  = { BIZ, 6, 7.5f },
    [KIND_HEAVY]   = { JET, 5, 11.5f },
    [KIND_FAST]    = { FAST, 3, 8.0f },
    [KIND_HELI]    = { HELI, 3, 7.0f },
    [KIND_GLIDER]  = { GLIDER, 3, 8.0f },
    [KIND_BALLOON] = { NULL, 0, 5.0f },
    [KIND_DRONE]   = { DRONE, 2, 5.0f },
    [KIND_GROUND]  = { GROUND, 1, 4.0f },
};

void silhouette(canvas_t *cv, int kind, float x, float y, float heading, float size, uint32_t c, int a)
{
    if (kind < 0 || kind >= KINDS) kind = KIND_JET;
    const shape_t *sh = &SHAPES[kind];
    float s = sh->size * size;
    if (!gfx_touches(cv, (int)(y - s - 2), (int)(y + s + 2))) return;
    if (kind == KIND_BALLOON) { gfx_disc(cv, x, y, s, c, a); return; }
    float ch = cosf(heading * D2R), sn = sinf(heading * D2R);
    for (int i = 0; i < sh->np; i++) {
        const part_t *p = &sh->parts[i];
        float xy[14];
        for (int k = 0; k < p->n; k++) {
            float px = p->xy[2 * k], py = p->xy[2 * k + 1];
            xy[2 * k] = x + (px * ch - py * sn) * s;
            xy[2 * k + 1] = y + (px * sn + py * ch) * s;
        }
        gfx_poly(cv, xy, p->n, c, a);
    }
    if (kind == KIND_HELI) gfx_ring(cv, x, y - 0.12f * s * ch, 0.9f * s, 1.0f, c, a / 2);   /* the rotor */
}
```

## Tables of the disc

```c
/* --- the disc: glass, rings, ticks, the cross and the beam in one pass over its pixels, the
 * WiFi radar's way, in whole numbers (the C3 has no floating point unit). the radius of a pixel
 * in 1/16 px follows from the one before it, the angle comes from a table of an eighth of a
 * turn, a byte a pixel: the other eighth of the quadrant is its mirror (dx and dy swapped, 90
 * degrees less the angle), so the table is half a quadrant and the heap keeps the rest for the
 * map. the light of what never turns (glass, rings, their glow) is a value by radius. the ticks
 * and the cross turn with the map: the angle only says which one is near, the exact distance to
 * it comes from that line's sine and cosine */

static uint8_t *s_phi8;                  /* angle from straight up, 1/256 of 90 degrees, dx <= dy only */
static uint16_t s_tri[RQ];               /* where row dy starts in it */
static uint8_t  s_ring[RQ * 16], s_dash[RQ * 16], s_glass[RQ + 1];

static float cover(float r, float R, float w) { return clampf(w * 0.5f + 0.5f - fabsf(r - R), 0, 1); }

int scope_init(void)
{
    if (!s_phi8) s_phi8 = malloc(RQ * (RQ + 1) / 2);
    if (!s_phi8) return 0;
    int at = 0;
    for (int dy = 0; dy < RQ; dy++) {
        s_tri[dy] = (uint16_t)at;
        for (int dx = 0; dx <= dy; dx++) {
            float a = atan2f((float)dx, (float)dy) / (0.5f * 3.14159265f) * 256.0f;
            s_phi8[at++] = (uint8_t)(a + 0.5f > 255.0f ? 255.0f : a + 0.5f);
        }
    }
    for (int i = 0; i < RQ * 16; i++) {
        float r = (float)i / 16.0f, a = 0, d = 0;
        a += 150.0f * cover(r, (float)PR, 1.0f);                    // the range ring
        a += 230.0f * cover(r, (float)SR, 1.3f);                    // the rim
        if (r > SR) { float h = clampf(1.0f - (r - SR) / 5.0f, 0, 1); a += 30.0f * h * h; }        // glow outside it
        if (r < PR && r > PR - 10) { float h = (r - (PR - 10)) / 10.0f; a += 22.0f * h * h; }    // and inside the ring
        for (int k = 1; k <= 3; k++) d += 95.0f * cover(r, PR * (float)k / 4.0f, 1.0f);      // the inner rings, dashed
        s_ring[i] = (uint8_t)clampf(a, 0, 255);
        s_dash[i] = (uint8_t)clampf(d, 0, 255);
    }
    for (int r = 0; r <= RQ; r++) {
        float t = clampf((float)r / PR, 0, 1);
        s_glass[r] = (uint8_t)(70.0f + 100.0f * t * t * t);         // dark in the middle, lighter to the ring
    }
    return 1;
}

/* the 72 tick lines, every 5 degrees of the compass, as they stand on the screen: cosine and
 * sine in 1/16384. made again only when the map turns */
static int32_t s_tc[72], s_ts[72];
static int     s_tick_up = -1;

static void tick_lines(int up)
{
    if (up == s_tick_up) return;
    for (int k = 0; k < 72; k++) {
        float th = (float)(k * 5 - up) * D2R;
        s_tc[k] = (int32_t)lroundf(cosf(th) * 16384.0f);
        s_ts[k] = (int32_t)lroundf(sinf(th) * 16384.0f);
    }
    s_tick_up = up;
}

// how far a pixel (dx, dy from the middle) is from tick line k, in 1/256 px
static inline int32_t off_line(int dx, int dy, int k)
{
    int32_t d = (dx * s_tc[k] + dy * s_ts[k]) >> 6;
    return d < 0 ? -d : d;
}
```

## The disc pass

Glass, rings, ticks, cross and beam in one walk over the disc's pixels. `disc_pass` is
`ALWAYS_INLINE` and called with constants, so the compiler makes one copy without the map.

```c
#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

/* the pass itself. disc() below makes it twice: once for the plain radar, where the compiler
 * drops everything of the map, and once for the rest. a loop that may meet the map runs out of
 * registers and is slower by a sixth even with no map in it */
static ALWAYS_INLINE void disc_pass(ui_t *u, canvas_t *cv, const int radar, const int classic, const map_t *m)
{
    int ya = SCY - RQ + 1, yb = SCY + RQ - 1;
    if (ya < cv->y0) ya = cv->y0;
    if (yb > cv->y0 + cv->h - 1) yb = cv->y0 + cv->h - 1;
    if (ya > yb) return;
    /* which texel lies under a pixel: the map is north up and a texel a pixel at the range it
     * was made for, the scope turns and stretches it. 1/65536 texel, a step per pixel along x */
    uint32_t gcol[M_CLASSES], gpaint[M_CLASSES];   // gpaint: what a texel adds, or is (classic), off the beam's trail
    uint8_t  glev[M_CLASSES], echo_lut[257];
    float    gk = 0, gcu = 1, gsu = 0;
    int32_t  gdu = 0, gdv = 0, gmu = 0, gmv = 0;
    if (m) {
        gk = m->kpx / k_px(u);
        gcu = cosf(u->cfg.up * D2R);
        gsu = sinf(u->cfg.up * D2R);
        gdu = (int32_t)lroundf(gcu * gk * 65536.0f);
        gdv = (int32_t)lroundf(gsu * gk * 65536.0f);
        gmu = m->w * 32768;                          // home: the middle of the map
        gmv = m->h * 32768;
        ground_paint(u, gcol, glev);
        for (int i = 0; i < M_CLASSES; i++)
            gpaint[i] = classic ? gcol[i] : gfx_scale(gcol[i], radar ? glev[i] * 110u >> 8 : glev[i]);
        for (int i = 0; i <= 256; i++) {               // the radar's ground: lit by the beam, then fading
            int f = 256 - i;
            echo_lut[i] = (uint8_t)(110 + (146 * f * f >> 16));
        }
    }
    const uint16_t beam = (uint16_t)(u->beam / 360.0f * 65536.0f);
    const uint16_t up16 = (uint16_t)((uint32_t)u->cfg.up * 65536u / 360u);
    const uint16_t trail = 16384;                                  /* 90 degrees of afterglow */
    const uint32_t RP16 = PR * 16, RQ16 = RQ * 16, RIM16 = SR * 16 + 4;
    const uint32_t glass = C_GLASS, prim = C_PRIM, focus = C_FOCUS;
    const int32_t bc = (int32_t)lroundf(cosf(u->beam * D2R) * 16384.0f), bs = (int32_t)lroundf(sinf(u->beam * D2R) * 16384.0f);
    uint8_t trail_lut[257];                                        // by distance behind the beam: no divide a pixel
    for (int i = 0; i <= 256; i++) {
        int f = 256 - i;
        trail_lut[i] = (uint8_t)(80 * f * f >> 16);
    }
    const uint32_t trail_k = (256u << 16) / trail;
    /* the ground of a row is looked up first, in a loop of its own: the pixel loop below has
     * all the registers it can use already */
    uint8_t ground_row[2 * RQ + 2];
    memset(ground_row, M_NONE, sizeof ground_row);

    for (int y = ya; y <= yb; y++) {
        int dy = y - SCY, ady = dy < 0 ? -dy : dy;
        if (ady >= RQ) continue;
        uint32_t *row = gfx_row(cv, y);
        const uint8_t *lp = s_phi8 + s_tri[ady];
        int span = (int)sqrtf((float)((RQ - 1) * (RQ - 1) - ady * ady));
        int xa = SCX - span < 0 ? 0 : SCX - span;
        int xb = SCX + span > GFX_W - 1 ? GFX_W - 1 : SCX + span;
        /* a pixel's radius (1/16 px) and angle hang on |dx| only: worked out once a row for both
         * halves of it, not once a pixel. a radius past the disc is RQ16 */
        uint16_t rad[RQ + 1], phi[RQ + 1];
        const int amax = SCX - xa > xb - SCX ? SCX - xa : xb - SCX;
#if GFX_SOFT_FLOAT
        int32_t r2 = dy * dy;                                      // the radius squared, whole px
        uint32_t r16 = gfx_isqrt((uint32_t)r2 << 8);
#endif
        for (int a = 0; a <= amax; a++) {
#if GFX_SOFT_FLOAT
            if (a) {                                               // a pixel out: the radius moves under 1 px
                r2 += 2 * a - 1;
                uint32_t t = (uint32_t)r2 << 8;
                while ((r16 + 1) * (r16 + 1) <= t) r16++;
                while (r16 * r16 > t) r16--;
            }
#else
            uint32_t r16 = (uint32_t)(gfx_sqrt((float)(a * a + dy * dy)) * 16.0f);   // with a float unit this is quicker
#endif
            rad[a] = (uint16_t)(r16 < RQ16 ? r16 : RQ16);
            phi[a] = (uint16_t)(a <= ady ? lp[a] : 256u - s_phi8[s_tri[a] + ady]);
        }
        const uint8_t *cl = ground_row - xa;                       // the ground under pixel x: cl[x]
        if (m && ady < PR) {
            int gs = (int)sqrtf((float)(PR * PR - ady * ady)), ga = SCX - gs, gb = SCX + gs;   // the range ring's chord
            float right = (float)(ga - SCX), up = (float)-dy;     // to_screen backwards: east and north
            int32_t gu = gmu + (int32_t)lroundf((right * gcu + up * gsu) * gk * 65536.0f);
            int32_t gv = gmv - (int32_t)lroundf((up * gcu - right * gsu) * gk * 65536.0f);
            const uint32_t mw = (uint32_t)m->w, mh = (uint32_t)m->h;
            memset(ground_row, M_NONE, sizeof ground_row);
            for (int x = ga; x <= gb; x++, gu += gdu, gv += gdv) {
                uint32_t iu = (uint32_t)(gu >> 16), iv = (uint32_t)(gv >> 16);
                if (iu < mw && iv < mh) {
                    ground_row[x - xa] = (uint8_t)((m->row[iv][iu >> 3] >> ((iu & 7) * 4)) & 15);
                }
            }
        } else if (m) {
            memset(ground_row, M_NONE, sizeof ground_row);
        }
        for (int x = xa; x <= xb; x++) {
            int dx = x - SCX, adx = dx < 0 ? -dx : dx;
            const uint32_t r16 = rad[adx];
            if (r16 >= RQ16) continue;
            unsigned ph = phi[adx], q = ph << 6;                   /* a quarter turn = 16384 */
            uint16_t ang;
            if (dx >= 0) ang = dy < 0 ? (uint16_t)q : (uint16_t)(32768u - q);
            else ang = dy >= 0 ? (uint16_t)(32768u + q) : (uint16_t)(0u - q);
            const uint16_t brg = (uint16_t)(ang + up16);           // the compass direction of the pixel

            uint32_t p = row[x];
            const int cls = m ? cl[x] : M_NONE;            // no map: the compiler drops what follows from it
            if (r16 <= RP16) {
                p = gfx_mix(p, glass, s_glass[r16 >> 4]);
                if (cls != M_NONE && !radar) p = classic ? gpaint[cls] : gfx_add(p, gpaint[cls]);
            }
            unsigned st = s_ring[r16];
            if (radar && (ph >> 3) & 1) st += s_dash[r16];
            if (r16 >= (SR - 6) * 16 && r16 <= RIM16) {            // the ticks, in from the rim
                int k = (int)(((uint32_t)brg * 72u + 32768u) >> 16) % 72;
                int len = k % 6 == 0 ? 6 : k % 2 == 0 ? 4 : 2;
                int32_t cov = 192 - off_line(dx, dy, k);
                if (cov > 0 && r16 >= (uint32_t)(SR - len) * 16) st += (unsigned)((k % 6 == 0 ? 240 : k % 2 == 0 ? 150 : 90) * cov >> 8);
            } else if (radar && r16 >= 7 * 16 && r16 < RP16 - 16) {   // the cross along the compass, the diagonals fainter
                int k8 = (int)(((uint32_t)brg * 8u + 32768u) >> 16) & 7;
                int32_t cov = 256 - off_line(dx, dy, k8 * 9);
                if (cov > 0) st += (unsigned)((k8 & 1 ? 20 : 36) * cov >> 8);
            }
            if (st) p = gfx_add(p, gfx_scale(prim, st > 255 ? 255 : st));
            if (radar && r16 <= RP16) {
                uint16_t d = (uint16_t)(beam - ang);                /* how far behind the beam */
                if (d < trail) {
                    uint32_t behind = ((uint32_t)d * trail_k) >> 16;
                    if (cls != M_NONE)                              // the ground echoes, brightest just after the beam
                        p = gfx_add(p, gfx_scale(gcol[cls], (uint32_t)glev[cls] * echo_lut[behind] >> 8));
                    p = gfx_add(p, gfx_scale(prim, trail_lut[behind]));
                } else if (cls != M_NONE) {
                    p = gfx_add(p, gpaint[cls]);
                }
                int16_t e = (int16_t)(uint16_t)(ang - beam);        /* near the beam's edge: how near, exactly */
                if (e > -1200 && e < 1200 && r16 > 32) {
                    int32_t off = (dx * bc + dy * bs) >> 6, cov = 333 - (off < 0 ? -off : off);
                    if (cov > 0) p = gfx_add(p, gfx_scale(focus, (uint32_t)(190 * cov >> 8)));
                }
            }
            row[x] = p;
        }
    }
}

CORE_HOT static void disc(ui_t *u, canvas_t *cv)
{
    const map_t *m = ground(u);
    const int look = u->cfg.look;
    if (look == LOOK_RADAR && !m) disc_pass(u, cv, 1, 0, NULL);
    else disc_pass(u, cv, look == LOOK_RADAR, look == LOOK_CLASSIC, m);
}
```

## Where things are, and what is worked out once a frame

```c
static float k_px(const ui_t *u) { return (float)PR / range_km(&u->cfg); }

// km east/north of home -> screen pixel, the map turned so view_up is at the top
static void to_screen(const ui_t *u, float x, float y, float *sx, float *sy)
{
    static float cu = 1.0f, su = 0.0f;                 // the turn of the map, worked out when it changes
    static int at = 0;
    const int vu = view_up(u);
    if (vu != at) { at = vu; cu = cosf(at * D2R); su = sinf(at * D2R); }
    float k = k_px(u);
    float right = x * cu - y * su, up = x * su + y * cu;
    *sx = view_cx(u) + right * k;
    *sy = SCY - up * k;
}

// a compass bearing -> the angle on the screen, clockwise from straight up
static float screen_ang(const ui_t *u, float brg) { return wrap360(brg - (float)view_up(u)); }

// inside what shows the map: the range ring, or on the flat square the whole left side
static int in_view(const ui_t *u, float sx, float sy)
{
    if (square_look(u)) return sx >= 0 && sx < GFX_W && sy >= 0 && sy < GFX_H;
    return (sx - SCX) * (sx - SCX) + (sy - SCY) * (sy - SCY) <= (float)(PR * PR);
}

static float plane_size(void) { return (float)GFX_H / 240.0f; }
```

```c
#define LABEL_W 60
#define LABEL_H 18

static const char *plane_name(const plane_t *p) { return p->flight[0] ? p->flight : p->reg[0] ? p->reg : "------"; }

// the numbers on the rings: quarters of the range, as short as they can be written
static void ring_value(float v, char *buf, int sz)
{
    if (fabsf(v - roundf(v)) < 0.05f) snprintf(buf, (size_t)sz, "%d", (int)lroundf(v));
    else snprintf(buf, (size_t)sz, v < 100 ? "%.1f" : "%.0f", (double)v);
}

static void ring_label_at(int k, int *x, int *y)
{
    float r = PR * (float)k / 4.0f;
    *x = SCX + (int)(r * 0.7071f) + 2;
    *y = SCY + (int)(r * 0.7071f) - 2;
}
```

```c
void scope_frame(ui_t *u)
{
    tick_lines(u->cfg.up);                             // the disc's ticks and the compass letters stand on them
    grid_frame(u);
    sky_t *s = &u->snap;
    // nearest first
    u->n_order = 0;
    for (int i = 0; i < s->n; i++) {
        int j = u->n_order++;
        while (j > 0 && s->p[u->order[j - 1]].dist > s->p[i].dist) { u->order[j] = u->order[j - 1]; j--; }
        u->order[j] = (uint8_t)i;
    }
    float range = range_km(&u->cfg);
    int sel = -1;
    for (int k = 0; k < u->n_order; k++) {
        int i = u->order[k];
        const plane_t *p = &s->p[i];
        spot_t *sp = &u->spot[i];
        float x, y, sx, sy;
        plane_now(p, u->now, &x, &y);
        to_screen(u, x, y, &sx, &sy);
        float d = sqrtf(x * x + y * y);
        sp->lw = 0;
        if (square_look(u)) {                          // the flat square: on it, or a dart at its edge
            int on = sx >= 4 && sx < view_right(u) - 4 && sy >= 4 && sy < GFX_H - 4;
            sp->vis = on ? 1 : d <= range * 2.4f ? 2 : 0;
            if (sp->vis == 2) {
                sx = clampf(sx, 5, (float)(view_right(u) - 6));
                sy = clampf(sy, 5, (float)(GFX_H - 6));
            }
        } else if (d <= range) {
            sp->vis = 1;
        } else if (d <= range * 1.6f) {                 // just outside: a dart in the band past the ring
            sp->vis = 2;
            float a = atan2f(sx - (float)SCX, (float)SCY - sy);
            sx = SCX + (PR + 4) * sinf(a);
            sy = SCY - (PR + 4) * cosf(a);
        } else {
            sp->vis = 0;
        }
        sp->x = (int16_t)lroundf(sx);
        sp->y = (int16_t)lroundf(sy);
        if (p->icao == u->sel) sel = i;
        // the trail on the screen, once a frame, not once a strip: the 3.5" has 40 of them
        sp->tn = sp->vis == 1 ? p->tn : 0;
        sp->ty0 = sp->ty1 = sp->y;
        for (int t = 0; t < sp->tn; t++) {
            float tx, ty;
            to_screen(u, p->tx[t] * 0.01f, p->ty[t] * 0.01f, &tx, &ty);
            if (!in_view(u, tx, ty)) {                  // only inside the scope
                sp->tx8[t] = TRAIL_GAP;
                continue;
            }
            sp->tx8[t] = (int16_t)lroundf(tx * 8.0f);
            sp->ty8[t] = (int16_t)lroundf(ty * 8.0f);
            if (ty < sp->ty0) sp->ty0 = (int16_t)ty;
            if (ty + 1 > sp->ty1) sp->ty1 = (int16_t)(ty + 1);
        }
    }

    /* labels: the picked one first, then the nearest, each tries six places round its plane
     * and takes the first that covers nothing: other labels, planes, ring numbers, corners */
    static box_t used[SKY_MAX * 2 + 12];
    int nu = 0;
#if RADAR_ROUND
    for (int k = 1; k <= 4; k++) {
        int x, y;
        ring_label_at(k, &x, &y);
        used[nu++] = (box_t){ x - 1, y - 1, 24, 8 };
    }
    used[nu++] = (box_t){ SCX - 78, SCY + 20, 156, 62 };  // the card or the note under home
    used[nu++] = (box_t){ SCX - 42, SCY - PR + 11, 84, 12 };   // OpenStreetMap's name
#else
    if (square_look(u)) {                               // the flat square: its corners and edges
        for (int k = 0; k < SQ_BOXES; k++) used[nu++] = sq_box(u, k);
    } else {
        for (int k = 1; k <= 4; k++) {
            int x, y;
            ring_label_at(k, &x, &y);
            used[nu++] = (box_t){ x - 1, y - 1, 24, 8 };
        }
        used[nu++] = (box_t){ 0, 0, 92, 30 };           // the corner texts
        used[nu++] = (box_t){ PX - 82, 0, 82, 34 };
        used[nu++] = (box_t){ 0, GFX_H - 24, 150, 24 };
        used[nu++] = (box_t){ PX - 66, GFX_H - 31, 66, 31 };
        used[nu++] = (box_t){ SCX - 42, SCY + PR - 18, 84, 12 };
    }
#endif
    for (int k = 0; k < u->n_order; k++) {
        const spot_t *sp = &u->spot[u->order[k]];
        if (sp->vis == 1) used[nu++] = (box_t){ sp->x - 6, sp->y - 6, 12, 12 };
    }
    int most = GFX_H > 240 ? 9 : RADAR_ROUND ? 4 : 6;    // a bigger scope has room for more
    int budget = u->cfg.labels ? 1 : (sel >= 0 ? most - 1 : most), placed = 0;
    for (int j = -1; j < u->n_order && placed < budget; j++) {
        int i = j < 0 ? sel : u->order[j];
        if (i < 0 || (j >= 0 && i == sel)) continue;
        if (u->cfg.labels && i != sel) continue;
        const plane_t *p = &s->p[i];
        spot_t *sp = &u->spot[i];
        if (sp->vis != 1) continue;
        char a[12];
        fmt_alt(u, p, a, sizeof a, 1);
        int w = font_width(&FONT_SMALL, plane_name(p));
        if (w > LABEL_W) w = LABEL_W;
        int wa = micro_width(a, 1);
        if (wa > w) w = wa;
        w += 5;
        const int cand[6][2] = { { 8, -9 }, { -8 - w, -9 }, { 5, -LABEL_H - 5 }, { 5, 6 }, { -5 - w, -LABEL_H - 5 }, { -5 - w, 6 } };
        for (int t = 0; t < 6; t++) {
            box_t r = { sp->x + cand[t][0], sp->y + cand[t][1], w, LABEL_H };
            if (r.x < 3 || r.x + r.w > (square_look(u) ? view_right(u) : PX) - 5 || r.y < 3 || r.y + r.h > GFX_H - 3) continue;
#if RADAR_ROUND
            int out = 0;                                // every corner of it on the glass
            for (int q = 0; q < 4; q++) {
                int qx = (q & 1 ? r.x + r.w : r.x) - SCX, qy = (q & 2 ? r.y + r.h : r.y) - SCY;
                out |= qx * qx + qy * qy > (SR - 2) * (SR - 2);
            }
            if (out) continue;
#endif
            int clash = 0;
            for (int q = 0; q < nu && !clash; q++) {
                const box_t *o = &used[q];
                // its own plane is in the list too: that one may touch
                if (o->x == sp->x - 6 && o->y == sp->y - 6 && o->w == 12) continue;
                clash = overlap(r, *o);
            }
            if (clash) continue;
            sp->lx = (int16_t)r.x; sp->ly = (int16_t)r.y; sp->lw = (int16_t)r.w;
            if (nu < (int)(sizeof used / sizeof used[0])) used[nu++] = r;
            placed++;
            break;
        }
    }
}

int scope_pick(const ui_t *u, int x, int y)
{
    int best = -1, bd = 18 * 18;
    for (int i = 0; i < u->snap.n; i++) {
        const spot_t *sp = &u->spot[i];
        if (!sp->vis) continue;
        int dx = sp->x - x, dy = sp->y - y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
```

## Compass letters and ring numbers

```c
// the compass letters and numbers, they turn with the map (its ticks and cross are in disc()). and the mark for the top
static void compass(ui_t *u, canvas_t *cv)
{
    char buf[8];
    if (!gfx_touches(cv, SCY - SR - 10, SCY + SR)) return;
    for (int d = 0; d < 360; d += 30) {                // where they stand comes from the tick lines (scope_frame)
        int k = d / 5;
        int lx = SCX + ((RL * s_ts[k] + 8192) >> 14), ly = SCY - ((RL * s_tc[k] + 8192) >> 14);
        if (!gfx_touches(cv, ly - 14, ly + 8)) continue;   // not in this strip: no letters looked up for nothing
        if (d % 90 == 0) {                             // the smooth font: the pixel one doubled reads N as an arch
            const char *L = d == 0 ? "N" : d == 90 ? "E" : d == 180 ? "S" : "W";
            const font_t *f = d == 0 ? &FONT_MEDIUM : &FONT_SMALL;
            font_text(cv, f, lx - font_width(f, L) / 2, ly - f->ascent + (d == 0 ? 5 : 4), L, d == 0 ? C_FOCUS : C_PRIM, 255);
        } else if (u->cfg.look == LOOK_RADAR) {        // the maps have their grid instead
            snprintf(buf, sizeof buf, "%d", d);
            micro_text(cv, lx - micro_width(buf, 1) / 2, ly - 2, buf, C_PRIM, 200, 1);
        }
    }
    float ty = (float)(SCY - SR - 8);
    if (ty >= 0) gfx_tri(cv, SCX - 4.0f, ty, SCX + 4.0f, ty, (float)SCX, ty + 5.0f, C_FOCUS, 220);   // the top of the screen
}

static void ring_numbers(ui_t *u, canvas_t *cv)
{
    char buf[12];
    float range = u->cfg.range;
    for (int k = u->cfg.look == LOOK_RADAR ? 1 : 4; k <= 4; k++) {   // the maps: only the ring, the grid tells the rest
        int x, y;
        ring_label_at(k, &x, &y);
        if (!gfx_touches(cv, y - 1, y + 7)) continue;
        ring_value(range * (float)k / 4.0f, buf, sizeof buf);
        micro_text(cv, x, y, buf, k == 4 ? C_PRIM : C_DIM, 220, 1);
    }
}

#define INK RGB(16, 20, 30)            /* outlines on the classic map's light ground */

static void home(ui_t *u, canvas_t *cv)
{
    const float cx = (float)view_cx(u);
    if (u->cfg.look == LOOK_CLASSIC) gfx_ring(cv, cx, SCY, 4.5f, 3.2f, INK, 170);
    gfx_ring(cv, cx, SCY, 4.5f, 1.2f, C_FOCUS, 230);
    gfx_disc(cv, cx, SCY, 1.5f, C_FOCUS, 255);
}
```

## Planes: trails, glow, silhouette, the ring when new, four arcs when picked, label cards

```c
static void planes(ui_t *u, canvas_t *cv)
{
    const sky_t *s = &u->snap;
    float k = k_px(u), size = plane_size();
    if (!gfx_touches(cv, SCY - RQ - 20, SCY + RQ + 20)) return;
    for (int q = u->n_order - 1; q >= 0; q--) {         // farthest first, the nearest on top
        int i = u->order[q];
        const plane_t *p = &s->p[i];
        const spot_t *sp = &u->spot[i];
        if (!sp->vis) continue;
        uint32_t col = alt_colour(u, p);
        int near = gfx_touches(cv, sp->y - 26, sp->y + 26);   // its glow, shape and rings: only the strips they are in
        if (sp->vis == 2) {                             // outside the range: a dart past the ring
            if (!near) continue;
            float a = atan2f((float)(sp->x - view_cx(u)), (float)(SCY - sp->y));
            float ox = sinf(a), oy = -cosf(a), px = -oy, py = ox;
            gfx_tri(cv, sp->x + ox * 4, sp->y + oy * 4, sp->x - ox * 3 + px * 3, sp->y - oy * 3 + py * 3,
                    sp->x - ox * 3 - px * 3, sp->y - oy * 3 - py * 3, col, 200);
            continue;
        }
        // the trail, older is fainter. worked out in scope_frame: a strip draws only the pieces in it
        uint32_t tc = u->cfg.look == LOOK_CLASSIC ? gfx_mix(col, INK, 110) : col;   // darker on the light map
        int tn = gfx_touches(cv, sp->ty0 - 2, sp->ty1 + 2) ? sp->tn : 0;
        int px8 = 0, py8 = 0, drawn = 0;
        for (int t = 0; t < tn; t++) {
            int x8 = sp->tx8[t], y8 = sp->ty8[t];
            if (x8 == TRAIL_GAP) { drawn = 0; continue; }
            int al = 40 + 120 * (t + 1) / (tn + 1);
            int ya = (drawn && py8 < y8 ? py8 : y8) >> 3, yb = (drawn && py8 > y8 ? py8 : y8) >> 3;
            if (gfx_touches(cv, ya - 2, yb + 2)) {
                if (drawn) gfx_line(cv, px8 * 0.125f, py8 * 0.125f, x8 * 0.125f, y8 * 0.125f, 1.0f, tc, al / 2);
                gfx_dot(cv, x8 * 0.125f, y8 * 0.125f, tc, al);
            }
            px8 = x8; py8 = y8;
            drawn = 1;
        }
        if (drawn) gfx_line(cv, px8 * 0.125f, py8 * 0.125f, sp->x, sp->y, 1.0f, tc, 80);
        int picked = p->icao == u->sel;
        float head = screen_ang(u, p->track);
        if (picked) {                                    // where it will be in a minute
            float km = p->gs * 1.852f / 60.0f, a = head * D2R;
            gfx_line(cv, (float)view_cx(u), SCY, sp->x, sp->y, 1.0f, C_FOCUS, 60);
            gfx_line(cv, sp->x, sp->y, sp->x + km * k * sinf(a), sp->y - km * k * cosf(a), 1.2f, C_FOCUS, 170);
            gfx_glow(cv, sp->x, sp->y, 16.0f, C_FOCUS, 100);
        }
        if (!near) continue;
        if (u->cfg.look == LOOK_CLASSIC) {               // light ground: a dark edge round it, light does not show
            silhouette(cv, p->kind, sp->x, sp->y, head, size * 1.35f, INK, 200);
        } else {
            // phosphor: brighter for a moment after the beam went over it
            float since = u->cfg.look == LOOK_RADAR ? wrap360(u->beam - wrap360(atan2f((float)(sp->x - SCX), (float)(SCY - sp->y)) / D2R)) : 180.0f;
            int glow = (int)(70.0f + 100.0f * (1.0f - since / 360.0f));
            gfx_glow(cv, sp->x, sp->y, 10.0f * size, col, glow);
        }
        silhouette(cv, p->kind, sp->x, sp->y, head, size, gfx_mix(col, C_FOCUS, 60), 255);
        uint32_t age = u->now - p->first;
        if (age < 1600u) {                               // just showed up: a ring opens round it
            float f = (float)age / 1600.0f;
            gfx_ring(cv, sp->x, sp->y, 5.0f + 16.0f * f, 1.0f, col, (int)(200 * (1.0f - f)));
        }
        if (picked) {                                    // four arcs turning round it, once in 3 s
            float rot = (float)(u->now % 3000u) * (360.0f / 3000.0f);
            for (int a4 = 0; a4 < 4; a4++)
                gfx_arc(cv, sp->x, sp->y, 12.0f, rot + a4 * 90.0f - 20.0f, rot + a4 * 90.0f + 20.0f, 1.4f, C_FOCUS, 230);
        }
    }
    // labels on top of every plane: a dark card, a bar of the plane's colour, a line to it
    for (int i = 0; i < s->n; i++) {
        const plane_t *p = &s->p[i];
        const spot_t *sp = &u->spot[i];
        if (!sp->lw || sp->vis != 1) continue;
        int ta = sp->ly < sp->y ? sp->ly : sp->y, tb = sp->ly + LABEL_H > sp->y ? sp->ly + LABEL_H : sp->y;
        if (!gfx_touches(cv, ta - 2, tb + 2)) continue;  // the card and its line to the plane
        uint32_t col = alt_colour(u, p);
        int picked = p->icao == u->sel;
        char a[12];
        fmt_alt(u, p, a, sizeof a, 1);
        gfx_fill(cv, sp->lx, sp->ly, sp->lw, LABEL_H, C_BG0, 150);
        gfx_vline(cv, sp->lx, sp->ly, sp->ly + LABEL_H - 1, col, 230);
        font_text_clip(cv, &FONT_SMALL, sp->lx + 3, sp->ly, plane_name(p), picked ? C_FOCUS : gfx_mix(col, C_FOCUS, 90), 255, LABEL_W);
        micro_text(cv, sp->lx + 3, sp->ly + 11, a, C_DIM, 230, 1);
        float nx = clampf((float)sp->x, (float)sp->lx, (float)(sp->lx + sp->lw - 1));
        float ny = clampf((float)sp->y, (float)sp->ly, (float)(sp->ly + LABEL_H - 1));
        float dx = nx - sp->x, dy = ny - sp->y, l = sqrtf(dx * dx + dy * dy);
        if (l > 7.0f) gfx_line(cv, sp->x + dx / l * 5.5f, sp->y + dy / l * 5.5f, nx, ny, 1.0f, col, 120);
    }
}

// a finger on the scope: the rim lights where it turns the map, a ring shows the zoom under it
static void gesture(ui_t *u, canvas_t *cv)
{
    if (!u->t_down || !u->t_moved || !u->t_turn) return;
    float dx = (float)(u->t_x - view_cx(u)), dy = (float)(SCY - u->t_y);
    if (u->t_turn == 1) {
        float a = atan2f(dx, dy) / D2R;
        gfx_arc(cv, SCX, SCY, SR + 3.0f, a - 24.0f, a + 24.0f, 2.2f, C_FOCUS, 220);
        float n = screen_ang(u, 0) * D2R;              // where north is now
        gfx_line(cv, SCX, SCY, SCX + PR * sinf(n), SCY - PR * cosf(n), 1.2f, C_FOCUS, 120);
    } else {
        float r = sqrtf(dx * dx + dy * dy);
        if (r > PR) r = PR;
        gfx_ring(cv, (float)view_cx(u), SCY, r, 1.3f, C_FOCUS, 170);
        char d[16];
        fmt_dist(u, range_km(&u->cfg) * r / PR, d, sizeof d);
        int w = micro_width(d, 1);
        int x = u->t_x - w / 2, y = u->t_y - 26;
        gfx_fill(cv, x - 3, y - 2, w + 6, 9, C_BG0, 190);
        micro_text(cv, x, y, d, C_FOCUS, 255, 1);
    }
}
```

## What the screen says about the sky

```c
// where the planes come from and how fresh they are, or what is wrong. bad: say it in the alarm colour
static void sky_status(const ui_t *u, char *buf, int sz, int *bad)
{
    if (u->net.demo) snprintf(buf, (size_t)sz, u->net.configured ? "NO WIFI" : "NO SETUP YET");
    else if (!u->net.wifi) snprintf(buf, (size_t)sz, u->snap.status == SKY_CONNECTING ? "CONNECTING" : "NO WIFI");
    else if (u->snap.status == SKY_OK) {
        uint32_t age = (u->now - u->snap.updated) / 1000u;
        snprintf(buf, (size_t)sz, "%s %uS", u->net.server, (unsigned)age);
    } else snprintf(buf, (size_t)sz, "%s", u->snap.note[0] ? u->snap.note : "WAITING");
    *bad = !u->net.demo && u->snap.status != SKY_CONNECTING && (!u->net.wifi || u->snap.status > SKY_OK);
}
```

## The brackets, corners, panel (square screens)

```c
static void bracket(canvas_t *cv, float x, float y, float sx, float sy)
{
    gfx_line(cv, x, y + sy * 20, x, y + sy * 6, 1.2f, C_PRIM, 170);
    gfx_line(cv, x, y + sy * 6, x + sx * 6, y, 1.2f, C_PRIM, 170);
    gfx_line(cv, x + sx * 6, y, x + sx * 20, y, 1.2f, C_PRIM, 170);
}

static void corners(ui_t *u, canvas_t *cv)
{
    char buf[48];
    bracket(cv, 3, 3, 1, 1);
    bracket(cv, (float)(PX - 6), 3, -1, 1);
    bracket(cv, 3, GFX_H - 4, 1, -1);
    bracket(cv, (float)(PX - 6), GFX_H - 4, -1, -1);
    gfx_hatch(cv, SCX - 31, 3, 62, 4, C_QUIET, 120);
    gfx_hatch(cv, SCX - 31, GFX_H - 7, 62, 4, C_QUIET, 120);
    if (gfx_touches(cv, 0, 30)) {
        int bad;
        // where home is, in the smooth font: a town's name may have letters the pixel font hasn't
        const char *where = u->net.demo ? "DEMO SKY" : u->cfg.place[0] ? u->cfg.place : "HOME";
        font_text_clip(cv, &FONT_SMALL, 9, 6, where, C_FOCUS, 255, SCX - 40);
        sky_status(u, buf, sizeof buf, &bad);
        micro_text(cv, 9, 18, buf, bad ? C_ALARM : C_DIM, 230, 1);
        // the look: a tap on its right half goes to the next one, on its left half to the one before (0.17)
        look_button(u, cv, LOOK_NAME[u->cfg.look < LOOKS ? u->cfg.look : 0], 0);
        const char *up = u->cfg.up ? buf : "NORTH UP";
        if (u->cfg.up) snprintf(buf, sizeof buf, "UP %03d*", u->cfg.up);
        micro_text(cv, PX - 9 - micro_width(up, 1), LOOK_Y + LOOK_H + 4, up, u->cfg.up ? C_FOCUS : C_QUIET, 235, 1);
    }
    if (gfx_touches(cv, GFX_H - 31, GFX_H)) {             // the height legend, and how the fingers work the scope
        static const char *const L[4] = { "GND", "<10K", "<25K", "HIGH" };
        const uint32_t C[4] = { C_DIM, C_LOW, C_PRIM, C_FOCUS };
        int x = 9;
        for (int i = 0; i < 4; i++) {
            gfx_disc(cv, (float)x + 2, GFX_H - 7.5f, 2.2f, C[i], 255);
            micro_text(cv, x + 7, GFX_H - 10, L[i], C_DIM, 220, 1);
            x += 9 + micro_width(L[i], 1) + 4;
        }
        micro_text(cv, 9, GFX_H - 17, "HEIGHT", C_QUIET, 220, 1);
        char d[16];
        fmt_dist(u, range_km(&u->cfg), d, sizeof d);
        micro_text(cv, PX - 9 - micro_width(d, 1), GFX_H - 10, d, C_PRIM, 240, 1);
        if (RADAR_HAS_TOUCH) {
            micro_text(cv, PX - 9 - micro_width("DRAG: ZOOM", 1), GFX_H - 17, "DRAG: ZOOM", C_QUIET, 200, 1);
            micro_text(cv, PX - 9 - micro_width("RIM: TURN", 1), GFX_H - 24, "RIM: TURN", C_QUIET, 200, 1);
        }
    }
}
```

```c
static void panel_title(canvas_t *cv, const char *title, const char *right)
{
    if (!gfx_touches(cv, 6, 16)) return;
    micro_text(cv, TX, 7, title, C_FOCUS, 240, 1);
    int w = micro_width(title, 1), rw = right ? micro_width(right, 1) : 0;
    if (right) micro_text(cv, TX2 - rw, 7, right, C_DIM, 230, 1);
    gfx_hatch(cv, TX + w + 4, 7, TX2 - (right ? rw + 4 : 0) - (TX + w + 4), 5, C_QUIET, 130);
    gfx_hline(cv, TX, TX2, 15, C_QUIET, 150);
}
```

```c
#define NEAR_Y0 72
#define NEAR_H  15
#define NEAR_N  (6 + (GFX_H - 240) / NEAR_H)   /* a taller screen lists more */

static void panel_overview(ui_t *u, canvas_t *cv)
{
    char buf[24];
    char d[16];
    if (ui_update(u) && !square_look(u)) update_note(u, cv);   // a newer version: over the radar the title says so
    else panel_title(cv, RADAR_NAME, 0);
    if (gfx_touches(cv, 18, 68)) {                     // how many, in what range, around where
        int n = 0;
        for (int i = 0; i < u->snap.n; i++) n += u->spot[i].vis == 1;
        snprintf(buf, sizeof buf, "%d", n);
        font_text(cv, &FONT_LARGE, TX - 1, 19, buf, C_FOCUS, 255);
        int w = font_width(&FONT_LARGE, buf);
        micro_text(cv, TX + w + 4, 22, "AIRCRAFT", C_PRIM, 240, 1);
        fmt_dist(u, range_km(&u->cfg), d, sizeof d);
        snprintf(buf, sizeof buf, "IN %s", d);
        micro_text(cv, TX + w + 4, 30, buf, C_DIM, 230, 1);
        // the town is in the corner: its country and the point here
        if (u->cfg.country[0]) font_text_clip(cv, &FONT_SMALL, TX, 38, u->cfg.country, C_DIM, 235, TX2 - TX);
        home_coords(u, buf, sizeof buf);
        micro_text(cv, TX, 50, buf, C_QUIET, 230, 1);
        micro_text(cv, TX, 60, "NEAREST", C_QUIET, 230, 1);
        gfx_hline(cv, TX, TX2, 67, C_QUIET, 110);
    }
    for (int k = 0; k < NEAR_N && k < u->n_order; k++) {
        const plane_t *p = &u->snap.p[u->order[k]];
        int y = NEAR_Y0 + k * NEAR_H;
        if (!gfx_touches(cv, y, y + NEAR_H)) continue;
        int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_ROW + k;
        if (pressed) gfx_fill(cv, TX - 2, y - 1, TX2 - TX + 4, NEAR_H - 1, C_PRIM, 60);
        gfx_disc(cv, TX + 2.5f, y + 6.0f, 2.2f, alt_colour(u, p), 255);
        font_text_clip(cv, &FONT_SMALL, TX + 8, y + 1, plane_name(p), C_FOCUS, 245, 44);
        fmt_dist(u, p->dist, d, sizeof d);
        char *sp = strchr(d, ' ');
        if (sp) *sp = 0;                                // just the number, the unit is in the title
        micro_text(cv, TX2 - micro_width(d, 1), y + 3, d, C_DIM, 230, 1);
    }
    if (!u->n_order) micro_text(cv, TX, NEAR_Y0 + 4, u->snap.status == SKY_OK ? "SKY IS EMPTY" : "WAITING", C_DIM, 230, 1);
    buttons(u, cv, "HUE", "LIST", "SET", -1);
}

static void panel_plane(ui_t *u, canvas_t *cv, const plane_t *p)
{
    char buf[32], b2[16];
    uint32_t col = alt_colour(u, p);
    panel_title(cv, "TRACKING", kind_name(p->kind));
    font_text_clip(cv, &FONT_MEDIUM, TX, 19, p->flight[0] ? p->flight : "NO CALLSIGN", C_FOCUS, 255, PW - 10);
    snprintf(buf, sizeof buf, "%s %s", p->type[0] ? p->type : "----", p->reg);
    font_text_clip(cv, &FONT_SMALL, TX, 34, buf, C_DIM, 235, PW - 10);
    int y = 46;
    if (p->orig[0] || p->dest[0]) {
        snprintf(buf, sizeof buf, "%s > %s", p->orig[0] ? p->orig : "?", p->dest[0] ? p->dest : "?");
        micro_text(cv, TX, y + 1, buf, C_PRIM, 240, 2);
        y += 14;
    }
    // the height, big, with climbing or descending beside it
    micro_text(cv, TX, y + 2, u->cfg.alt_unit == ALT_M ? "ALTITUDE, M" : "ALTITUDE, FT", C_QUIET, 230, 1);
    if (p->ground || !p->has_alt) {
        micro_text(cv, TX, y + 12, p->ground ? "ON GROUND" : "NO DATA", col, 245, 2);
    } else {
        int v = u->cfg.alt_unit == ALT_M ? (int)lroundf(p->alt * 0.3048f) : (int)p->alt;
        snprintf(buf, sizeof buf, "%d", v);
        font_text(cv, &FONT_LARGE, TX - 1, y + 9, buf, col, 255);
    }
    y += 38;
    if (abs(p->vrate) >= 200) {
        float ax = TX + 3.0f, ay = y + 3.0f;
        if (p->vrate > 0) gfx_tri(cv, ax - 3, ay + 3, ax + 3, ay + 3, ax, ay - 3, col, 240);
        else gfx_tri(cv, ax - 3, ay - 3, ax + 3, ay - 3, ax, ay + 3, col, 240);
        int vr = u->cfg.alt_unit == ALT_M ? (int)lroundf(abs(p->vrate) * 0.3048f) : abs(p->vrate);
        snprintf(buf, sizeof buf, "%d %s", vr, u->cfg.alt_unit == ALT_M ? "M/MIN" : "FT/MIN");
        micro_text(cv, TX + 9, y + 1, buf, C_DIM, 230, 1);
    } else {
        micro_text(cv, TX, y + 1, "LEVEL", C_DIM, 230, 1);
    }
    y += 10;
    snprintf(buf, sizeof buf, "%d KT  %03d*", (int)lroundf(p->gs), (int)lroundf(p->track) % 360);
    micro_text(cv, TX, y + 1, p->has_spd ? buf : "SPEED ---", C_PRIM, 240, 1);
    y += 10;
    fmt_dist(u, p->dist, b2, sizeof b2);
    snprintf(buf, sizeof buf, "%s %s %03d*", b2, compass8(p->brg), (int)lroundf(p->brg) % 360);
    micro_text(cv, TX, y + 1, buf, C_PRIM, 240, 1);
    y += 10;
    if (p->squawk == 7500 || p->squawk == 7600 || p->squawk == 7700) {
        static const char *const W[3] = { "HIJACK", "RADIO LOST", "EMERGENCY" };
        snprintf(buf, sizeof buf, "%04u %s", (unsigned)p->squawk, W[p->squawk == 7500 ? 0 : p->squawk == 7600 ? 1 : 2]);
        micro_text(cv, TX, y + 1, buf, C_ALARM, 255, 1);
    } else if (p->squawk) {
        snprintf(buf, sizeof buf, "SQUAWK %04u", (unsigned)p->squawk);
        micro_text(cv, TX, y + 1, buf, C_DIM, 230, 1);
    }
    buttons(u, cv, "NEXT", "WATCH", "X", watched(u, p) ? 1 : -1);   // lit: on the watch list
}
```

## The first seconds

```c
void boot_banner(ui_t *u, canvas_t *cv)
{
    uint32_t t = u->now - u->boot;
    if (t < 1800u) {
        float f = (float)t / 1800.0f;
        gfx_ring(cv, SCX, SCY, 8.0f + SR * f, 2.0f, C_FOCUS, (int)(230 * (1.0f - f)));
        int w = micro_width(RADAR_NAME, 2);
        gfx_fill(cv, SCX - w / 2 - 4, SCY + 14, w + 8, 18, C_BG0, (int)(200 * (1.0f - f * f)));
        micro_text(cv, SCX - w / 2, SCY + 18, RADAR_NAME, C_FOCUS, (int)(255 * (1.0f - f * f)), 2);
    }
    if (!ui_screen_name || u->now >= UI_SCREEN_PICK_MS || u->view != VIEW_MAIN) return;
    const int bw = 156, bh = 32, bx = SCX - bw / 2, by = SCY + 40;
    if (!gfx_touches(cv, by, by + bh)) return;
    chamfer_box(cv, bx, by, bw, bh, C_BG0, 220, C_FOCUS, 230);
    const char *t1 = "SCREEN WRONG? PRESS BOOT";
    micro_text(cv, SCX - micro_width(t1, 1) / 2, by + 7, t1, C_FOCUS, 250, 1);
    char t2[40];
    snprintf(t2, sizeof t2, "NOW %s", ui_screen_name);
    micro_text(cv, SCX - micro_width(t2, 1) / 2, by + 16, t2, C_DIM, 235, 1);
    int left = (int)((float)(bw - 16) * (float)(UI_SCREEN_PICK_MS - u->now) / (float)UI_SCREEN_PICK_MS);
    gfx_fill(cv, bx + 8, by + bh - 5, left, 1, C_FOCUS, 200);
}
```

# WiFi Radar, core/ui_radar.c and core/ui.c

## Polar tables and geometry

```c
static const int DBM_RINGS[4] = { -40, -55, -70, -85 };
/* polar lookup for one quadrant, one byte each: radius in whole px, angle from
 * vertical in 1/256 of 90 deg. two bytes each would cost 31 KB of ram, we dont have that */
static uint8_t  *s_r8, *s_phi8;
static uint8_t  *s_stat;              /* static light of the disc, see build_static */
static const rgeo_t *s_stat_geo;

static float cover(float r, float R, float w) { return clampf(w * 0.5f + 0.5f - fabsf(r - R), 0, 1); }

static void build_geo(rgeo_t *g)
{
    int rq = g->r_out + 5;
    for (int i = 0; i < rq * 16; i++) {
        float r = (float)i / 16.0f, a = 0, d = 0;
        a += 150.0f * cover(r, (float)g->r_plot, 1.0f);
        a += 230.0f * cover(r, (float)g->r_out, 1.3f);
        if (r > g->r_out) { float h = clampf(1.0f - (r - g->r_out) / 5.0f, 0, 1); a += 30.0f * h * h; }
        if (r < g->r_plot && r > g->r_plot - 10) { float h = (r - (g->r_plot - 10)) / 10.0f; a += 22.0f * h * h; }
        for (int k = 0; k < 4; k++) d += 95.0f * cover(r, rssi_frac((float)DBM_RINGS[k]) * g->r_plot, 1.0f);
        g->ring[i] = (uint8_t)clampf(a, 0, 255);
        g->dash[i] = (uint8_t)clampf(d, 0, 255);
    }
    for (int r = 0; r <= g->r_plot + 1; r++) {
        float t = (float)r / (float)g->r_plot;
        g->glass[r] = RADAR_EPAPER ? 255 : (uint8_t)(70.0f + 100.0f * t * t * t);   // paper: flat light grey face, like ViewOwl's dials
    }
}

static void set_geo(rgeo_t *g, int cx, int cy, int r_out, int lbl, int plot)
{
    g->cx = cx; g->cy = cy; g->r_out = r_out; g->r_lbl = lbl; g->r_plot = plot;
}

int radar_init(void)
{
    // full screen radar: as big as the height allows. on 320x240 this is 160,120 r=115
    int h2 = GFX_H / 2, rf = h2 - 5;
    set_geo(&G_FULL, GFX_W / 2, h2, rf, rf - 13, rf - 23);
#if RADAR_ROUND
    // round glass: no room for a side panel, main screen is the full radar. a bit smaller
    // so the outer ring clears the bezel (ViewOwl keeps everything inside r 112 on 240)
    rf = GFX_W / 2 - 12;
    set_geo(&G_FULL, GFX_W / 2, h2, rf, rf - 13, rf - 23);
    set_geo(&G_MAIN, GFX_W / 2, h2, rf, rf - 13, rf - 23);
    UI_PX = GFX_W;
    UI_PW = 0;
#elif RADAR_EPAPER
    // paper dashboard (ui_paper.c): grey header band on top, radar under it on the left,
    // cards from UI_PX on. the 4.2" keeps it smaller so the cards still fit next to it
    int rm = (GFX_H - PAPER_HEAD - 16) / 2;
    if (rm > GFX_W / 5 - 5) rm = GFX_W / 5 - 5;
    set_geo(&G_MAIN, rm + 12, PAPER_HEAD + 8 + rm, rm, rm - 13, rm - 22);
    UI_PX = G_MAIN.cx + rm + 14;
    UI_PW = GFX_W - UI_PX - 6;
#else
    // main screen: radar on the left, panel takes the rest (320x240: 113,120 r=106, panel at 228)
    int rm = h2 - 14;
    set_geo(&G_MAIN, rm + 7, h2, rm, rm - 13, rm - 22);
    UI_PX = 2 * G_MAIN.cx + 2;
    UI_PW = GFX_W - UI_PX - 1;
#endif
    if (s_r8) return 1;
    s_r8 = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    s_phi8 = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    s_stat = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    if (!s_r8 || !s_phi8 || !s_stat) return 0;
    for (int dy = 0; dy < RQ_MAX; dy++)
        for (int dx = 0; dx < RQ_MAX; dx++) {
            float r = sqrtf((float)(dx * dx + dy * dy));
            float a = atan2f((float)dx, (float)dy) / (0.5f * 3.14159265f) * 256.0f;
            s_r8[dy * RQ_MAX + dx] = (uint8_t)(r + 0.5f > 255.0f ? 255.0f : r + 0.5f);
            s_phi8[dy * RQ_MAX + dx] = (uint8_t)(a + 0.5f > 255.0f ? 255.0f : a + 0.5f);
        }
    build_geo(&G_MAIN);
    build_geo(&G_FULL);
    return 1;
}
```

## Baking the disc: what never moves goes into one byte a pixel of one quadrant

```c
// 72 ticks around (every 5 deg), long one every 30, medium every 10
static float tick_alpha(uint16_t ang, unsigned r16, int r_out)
{
    uint32_t t = (uint32_t)ang * 72u + 32768u;
    unsigned k = (t >> 16) % 72u;
    int frac = (int)(t & 0xFFFFu) - 32768;
    float px = (float)frac * (float)r16 * (6.2831853f / 72.0f / 65536.0f / 16.0f);
    float cov = 0.75f - fabsf(px);
    if (cov <= 0) return 0;
    int len = (k % 6 == 0) ? 6 : (k % 2 == 0) ? 4 : 2;
    float a = (k % 6 == 0) ? 240.0f : (k % 2 == 0) ? 150.0f : 90.0f;
    if (r16 < (unsigned)((r_out - len) * 16) || r16 > (unsigned)(r_out * 16 + 4)) return 0;
    return a * clampf(cov, 0, 1);
}

/* everything on the disc that never moves (rings, dashes, ticks, cross) baked into one
 * light value per pixel of one quadrant (s_stat). other 3 quadrants mirror it, 90 deg
 * holds a whole number of every tick and dash. doing it per pixel per frame was 22 ms on the board */

static void build_static(const rgeo_t *g)
{
    const int rq = g->r_out + 5;
    for (int ady = 0; ady < RQ_MAX; ady++)
        for (int adx = 0; adx < RQ_MAX; adx++) {
            float a = 0;
            if (ady < rq && adx < rq) {
                // exact math here, runs once. the lookup tables only have whole pixels
                float rf = sqrtf((float)(adx * adx + ady * ady));
                unsigned r16 = (unsigned)(rf * 16.0f + 0.5f);
                uint16_t phi = (uint16_t)(atan2f((float)adx, (float)ady) / (2.0f * 3.14159265f) * 65536.0f + 0.5f);
                if (r16 < (unsigned)rq * 16) {
                    a = g->ring[r16];
                    if ((phi >> 9) & 1) a += g->dash[r16];
                    a += tick_alpha(phi, r16, g->r_out);
                    if (r16 <= (unsigned)g->r_plot * 16) {
                        if (adx == 0 || ady == 0) a += 34.0f;
                        else if (adx == ady && !(adx & 1)) a += 20.0f;
                    }
                }
            }
            s_stat[ady * RQ_MAX + adx] = (uint8_t)clampf(a, 0, 255);
        }
    s_stat_geo = g;
}

/* glass, rings, cross, ticks and beam in one pass over the disc,
 * using the polar lookup so no trig per pixel */
CORE_HOT static void draw_disc(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const int rq = g->r_out + 5;
    int ya = g->cy - rq + 1, yb = g->cy + rq - 1;
    if (ya < cv->y0) ya = cv->y0;
    if (yb > cv->y0 + cv->h - 1) yb = cv->y0 + cv->h - 1;
    if (ya > yb) return;

    const int sweep = u->snap.sweep;
    const int run = sweep == SWEEP_RUN;
    const int beam_on = RADAR_EPAPER ? run : sweep != SWEEP_COUNTDOWN;   // paper: a frozen beam means nothing, only show it while turning
    const uint16_t beam = (uint16_t)(u->beam / 360.0f * 65536.0f);
    const uint16_t trail = run ? 7282 : 16384;          /* 40 or 90 degrees */
    const float trail_a = run ? 90.0f : 72.0f;
    const unsigned RP = (unsigned)g->r_plot, RMAX = (unsigned)(rq - 1);
    const float arc_k = 6.2831853f / 65536.0f;
    const uint32_t swept = gfx_scale(C_PRIM, 16);
    if (s_stat_geo != g) build_static(g);
    const uint32_t glass = C_GLASS, prim = C_PRIM;
    // trail light by distance behind the beam, 256 steps so theres no divide per pixel
    uint8_t trail_lut[257];
    for (int i = 0; i <= 256; i++) {
        float f = 1.0f - (float)i / 256.0f;
        trail_lut[i] = (uint8_t)(trail_a * f * f);
    }
    const uint32_t trail_k = (256u << 16) / trail;

    for (int y = ya; y <= yb; y++) {
        int dy = y - g->cy, ady = dy < 0 ? -dy : dy;
        if (ady >= rq) continue;
        uint32_t *row = gfx_row(cv, y);
        const uint8_t *lr = s_r8 + ady * RQ_MAX, *lp = s_phi8 + ady * RQ_MAX;
        const uint8_t *ls = s_stat + ady * RQ_MAX;
        int span = (int)sqrtf((float)((rq - 1) * (rq - 1) - ady * ady));
        int xa = g->cx - span < 0 ? 0 : g->cx - span;
        int xb = g->cx + span > GFX_W - 1 ? GFX_W - 1 : g->cx + span;
        for (int x = xa; x <= xb; x++) {
            int dx = x - g->cx, adx = dx < 0 ? -dx : dx;
            unsigned r = lr[adx];
            if (r > RMAX) continue;
            unsigned q = (unsigned)lp[adx] << 6;           /* quarter turn = 16384 */
            uint16_t ang;
            if (dx >= 0) ang = dy < 0 ? (uint16_t)q : (uint16_t)(32768u - q);
            else ang = dy >= 0 ? (uint16_t)(32768u + q) : (uint16_t)(0u - q);

            uint32_t p = row[x];
            if (r <= RP) p = gfx_mix(p, glass, g->glass[r]);
            unsigned st = ls[adx];
            if (st) p = gfx_add(p, gfx_scale(prim, st));

            if (beam_on && r <= RP) {
                uint16_t d = (uint16_t)(beam - ang);       /* how far behind the beam */
                if (d < trail) {
                    p = gfx_add(p, gfx_scale(prim, trail_lut[((uint32_t)d * trail_k) >> 16]));
                } else if (run && d <= beam) {
                    p = gfx_add(p, swept);
                }
                int16_t e = (int16_t)(uint16_t)(ang - beam);   // signed angle to the beam edge
                if (e > -1200 && e < 1200 && r > 2) {
                    float px = fabsf((float)e * (float)r * arc_k);
                    float w = run ? 1.6f : 1.1f;
                    if (px < w) p = gfx_add(p, gfx_scale(C_FOCUS, (uint32_t)((run ? 230 : 150) * clampf(w - px, 0, 1))));
                }
            }
            row[x] = p;
        }
    }
}
```

## Blips: solid with an arc of doubt, or hollow

```c
static void draw_blips(ui_t *u, canvas_t *cv, const rgeo_t *g, int full)
{
    if (!gfx_touches(cv, g->cy - g->r_plot - 30, g->cy + g->r_plot + 30)) return;
    const float sz = full ? 1.2f : 1.0f;

    if (u->target >= 0 && u->b[u->target].ghost)       // ghost target: "somewhere on this ring"
        gfx_ring(cv, (float)g->cx, (float)g->cy, u->b[u->target].r, 1.0f, C_FOCUS, 70);

    for (int j = u->n - 1; j >= 0; j--) {               // quietest first so loud ones end up on top
        int i = u->order[j];
        const blip_t *b = &u->b[i];
        float k = b->bright * (b->dim ? 0.4f : 1.0f);
        if (RADAR_EPAPER) {                             // paper: no glow, it only prints as grey fuzz. solid ink
            if (!b->ghost && (b->label || b->sel))
                gfx_arc(cv, (float)g->cx, (float)g->cy, b->r, b->ang - b->sd, b->ang + b->sd, 1.3f, b->col, 255);
            if (b->ghost) gfx_ring(cv, b->x, b->y, 3.4f * sz, 1.2f, b->col, 255);
            else gfx_disc(cv, b->x, b->y, 3.2f * sz, b->col, 255);
            continue;
        }
        if (!b->ghost) {
            if (b->label || b->sel)                     // angle error, as the arc where it could be
                gfx_arc(cv, (float)g->cx, (float)g->cy, b->r, b->ang - b->sd, b->ang + b->sd, 1.3f,
                        b->col, (int)(150 * (b->dim ? 0.4f : 1.0f)));
            gfx_glow(cv, b->x, b->y, 10.0f * sz, b->col, (int)(170 * k));
            gfx_disc(cv, b->x, b->y, 2.1f * sz, gfx_mix(b->col, C_FOCUS, 140), (int)(256 * clampf(k + 0.25f, 0, 1)));
            if (b->src >= 2)                            // placed by the walk map: extra ring
                gfx_ring(cv, b->x, b->y, 4.6f * sz, 0.8f, b->col, (int)(150 * k));
        } else {
            gfx_glow(cv, b->x, b->y, 7.0f * sz, b->col, (int)(70 * k));
            gfx_ring(cv, b->x, b->y, 3.2f * sz, 1.0f, b->col, (int)(230 * k));
        }
        if (!RADAR_EPAPER && b->age_ms < 1600) {        // expanding ring for the first 1.6 s
            float f = (float)b->age_ms / 1600.0f;
            gfx_ring(cv, b->x, b->y, 4.0f + 16.0f * f, 1.0f, b->col, (int)(200 * (1.0f - f)));
        }
    }

    if (u->target >= 0) {
        const blip_t *b = &u->b[u->target];
        float rot = (float)(u->now % 3000u) * (360.0f / 3000.0f);   // one turn per 3 s
        if (!b->ghost) gfx_line(cv, (float)g->cx, (float)g->cy, b->x, b->y, 1.0f, C_FOCUS, 60);
        for (int q = 0; q < 4; q++)
            gfx_arc(cv, b->x, b->y, 10.0f * sz, rot + q * 90.0f - 20.0f, rot + q * 90.0f + 20.0f, 1.4f, C_FOCUS, 230);
    }

    for (int i = 0; i < u->n; i++) {
        const blip_t *b = &u->b[i];
        if (!b->label) continue;
        float k = b->dim && !RADAR_EPAPER ? 0.55f : 1.0f;   // half ink on paper is dots, unreadable
        gfx_fill(cv, b->lx, b->ly, b->lw, b->lh, C_BG0, RADAR_EPAPER ? 255 : 150);   // paper: clean white under the words
        gfx_vline(cv, b->lx, b->ly, b->ly + b->lh - 1, b->col, 230);
        font_text_clip(cv, &FONT_SMALL, b->lx + 3, b->ly, b->name, b->sel ? C_FOCUS : b->col, (int)(255 * k), LABEL_W);
        micro_text(cv, b->lx + 3, b->ly + 11, b->sub, RADAR_EPAPER ? C_FOCUS : C_DIM, (int)(230 * k), 1);
        float nx = clampf(b->x, (float)b->lx, (float)(b->lx + b->lw - 1));
        float ny = clampf(b->y, (float)b->ly, (float)(b->ly + b->lh - 1));
        float dx = nx - b->x, dy = ny - b->y, l = sqrtf(dx * dx + dy * dy);
        if (l > 6.0f)
            gfx_line(cv, b->x + dx / l * 4.5f, b->y + dy / l * 4.5f, nx, ny, 1.0f, b->col, (int)(120 * k));
    }
}
```

## The 360 turn: the lobe, the countdown, the result ring

```c
/* during the 360 turn: signal of the target (or the loudest one) around the circle,
 * drawn while it is measured. the lobe points to the network */
static void draw_rose(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const app_t *a = &u->snap;
    int i = u->target >= 0 ? u->target : (u->n ? u->order[0] : -1);
    if (i < 0) return;
    const contact_t *c = &a->c[i];
    float raw[BEAR_BINS], m[BEAR_BINS], lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < BEAR_BINS; k++)
        raw[k] = c->bin_n[k] ? c->bin_sum[k] / c->bin_n[k] : 0.0f;
    for (int k = 0; k < BEAR_BINS; k++) {         // 1-2-1 average over 3 bins, so the lobe shows
        if (!c->bin_n[k]) continue;
        float s = 0, w = 0;
        for (int d = -1; d <= 1; d++) {
            int q = (k + d + BEAR_BINS) % BEAR_BINS;
            if (!c->bin_n[q]) continue;
            float wt = d ? 1.0f : 2.0f;
            s += raw[q] * wt;
            w += wt;
        }
        m[k] = s / w;
        if (m[k] < lo) lo = m[k];
        if (m[k] > hi) hi = m[k];
    }
    if (hi < lo) return;
    const float r0 = g->r_plot * 0.19f, rs = g->r_plot * 0.71f;
    float px = 0, py = 0, fx = 0, fy = 0;
    int prev = 0, first = 1;
    for (int k = 0; k < BEAR_BINS; k++) {
        if (!c->bin_n[k]) { prev = 0; continue; }
        float r = r0 + rs * (m[k] - lo) / (hi - lo + 2.0f);   // +2 so flat data doesnt blow up to full size
        float th = ((float)k + 0.5f) * (360.0f / BEAR_BINS) * D2R;
        float x = g->cx + r * sinf(th), y = g->cy - r * cosf(th);
        if (prev) gfx_line(cv, px, py, x, y, 1.4f, C_FOCUS, 190);
        gfx_disc(cv, x, y, 1.4f, C_FOCUS, 230);
        if (first && k == 0) { fx = x; fy = y; first = 0; }
        px = x; py = y; prev = 1;
    }
    if (c->bin_n[0] && c->bin_n[BEAR_BINS - 1] && !first)
        gfx_line(cv, px, py, fx, fy, 1.4f, C_FOCUS, 190);
}

static void draw_sweep_overlay(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const app_t *a = &u->snap;
    char buf[16];
    if (a->sweep == SWEEP_COUNTDOWN) {
        uint32_t el = u->now - a->sweep_t0;
        int n = 3 - (int)(el / 1000u);
        float f = (float)(el % 1000u) / 1000.0f;
        snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
        int w = font_width(&FONT_LARGE, buf);
        gfx_disc(cv, (float)g->cx, (float)g->cy, 19.0f, C_BG0, 200);
        gfx_ring(cv, (float)g->cx, (float)g->cy, 19.0f, 1.2f, C_PRIM, 200);
        gfx_ring(cv, (float)g->cx, (float)g->cy, 20.0f + 50.0f * f, 1.5f, C_FOCUS, (int)(200 * (1.0f - f)));
        font_text(cv, &FONT_LARGE, g->cx - w / 2, g->cy - 14, buf, C_FOCUS, 255);
    } else if (a->sweep == SWEEP_RUN) {
        draw_rose(u, cv, g);
        gfx_arc(cv, (float)g->cx, (float)g->cy, g->r_out + 3.0f, 0.0f, u->beam, 2.0f, C_FOCUS, 200);
        float th = u->beam * D2R, s = sinf(th), c = cosf(th);
        float tx = g->cx + (g->r_out - 1) * s, ty = g->cy - (g->r_out - 1) * c;
        float bx = g->cx + (g->r_out - 11) * s, by = g->cy - (g->r_out - 11) * c;
        gfx_tri(cv, tx - 5 * c, ty - 5 * s, tx + 5 * c, ty + 5 * s, bx, by, C_FOCUS, 240);
    } else if (a->sweep == SWEEP_RESULT) {
        float f = clampf((float)(u->now - a->sweep_t0) / 900.0f, 0, 1);
        gfx_ring(cv, (float)g->cx, (float)g->cy, g->r_plot * f, 2.0f, C_FOCUS, (int)(220 * (1.0f - f)));
    }
}
```

## Easing, ghost angle, fused direction (core/ui.c)

```c
static anim_t *anim_for(ui_t *u, uint32_t id, uint32_t now, int *fresh)
{
    anim_t *free_slot = 0;
    for (int i = 0; i < MAX_CONTACTS; i++) {
        if (u->anim[i].id == id) { *fresh = 0; return &u->anim[i]; }
        if (!u->anim[i].id && !free_slot) free_slot = &u->anim[i];
    }
    if (!free_slot) free_slot = &u->anim[0];    // hack: table full, just reuse slot 0
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->id = id;
    free_slot->born = now;
    *fresh = 1;
    return free_slot;
}

/* network with no direction yet: fixed angle from FNV hash of the BSSID, drawn hollow.
 * used to orbit slowly, but a circling dot looks like movement that isnt there */
static float ghost_angle(const uint8_t *m)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ m[i]) * 16777619u;
    return (float)(h % 3600u) * 0.1f;
}
```

```c
void ui_frame(ui_t *u, uint32_t now)
{
    app_t *a = &u->snap;
    ui_set_theme(u, u->theme);
    u->now = now;
    u->beam = a->sweep == SWEEP_RUN ? app_heading(a, now) : (float)(now % 4000u) * (360.0f / 4000.0f);
    u->n = app_sorted(a, u->order);
    u->target = -1;
    for (int i = 0; i < a->n; i++) if (a->c[i].id == a->target_id) u->target = i;
    if (u->target < 0 && u->view == VIEW_TARGET) u->view = VIEW_MAIN;

    for (int s = 0; s < MAX_CONTACTS; s++) {      // free anim slots of networks that are gone
        if (!u->anim[s].id) continue;
        int alive = 0;
        for (int i = 0; i < a->n && !alive; i++) alive = a->c[i].id == u->anim[s].id;
        if (!alive) u->anim[s].id = 0;
    }

    const mapview_t *mv = &a->map;
    int turn_fresh = a->bearing_ms && (!mv->active || !mv->turned || mv->since_turn < 2.5f);
    int used_map = 0, used_turn = 0;
    memset(u->spectrum, 0, sizeof u->spectrum);
    for (int i = 0; i < a->n; i++) {
        const contact_t *c = &a->c[i];
        blip_t *b = &u->b[i];
        int fresh;
        anim_t *an = anim_for(u, c->id, now, &fresh);
        float ft = rssi_frac(c->rssi), ga = ghost_angle(c->bssid), ang = 0, sd = 0;
        uint8_t src = 0;
        direction(u, c, turn_fresh, &ang, &sd, &src);
        if (fresh) { an->t = ft; an->ang = src ? ang : ga; }
        an->t += (ft - an->t) * (RADAR_EPAPER ? 1.0f : 0.18f);    // low-pass, raw values are jumpy. paper draws a frame in seconds, no easing there
        an->ang = src ? wrap360(an->ang + wrap180(ang - an->ang) * (RADAR_EPAPER ? 1.0f : 0.12f)) : ga;
        used_map += src >= 2;
        used_turn += src & 1;

        b->ang = an->ang;
        b->sd = sd;
        b->src = src;
        b->ghost = !src;
        b->col = sec_color(c->sec);
        b->sel = i == u->target;
        b->dim = u->target >= 0 && !b->sel;
        b->age_ms = (int)(now - an->born);
        if (RADAR_EPAPER) {
            b->bright = 1.0f;                       // no beam on paper, a half faded dot would only look random
        } else if (a->sweep == SWEEP_RUN || a->sweep == SWEEP_COUNTDOWN) {
            b->bright = 0.8f;
        } else {
            float f = 1.0f - wrap360(u->beam - b->ang) / 360.0f;    // fades after beam passes, like phosphor
            b->bright = 0.35f + 0.65f * f * f;
        }
        int32_t quiet = (int32_t)(now - c->last_ms);
        // not heard for 8 s: fade over next 30 s, down to 25%
        if (quiet > 8000) b->bright *= clampf(1.0f - (float)(quiet - 8000) / 30000.0f, 0.25f, 1.0f);
        snprintf(b->name, sizeof b->name, "%s", net_name(c));
        snprintf(b->sub, sizeof b->sub, "%d CH%d", (int)lroundf(c->rssi), c->channel);

        float v = clampf((c->rssi + 95.0f) / 60.0f, 0, 1);      // 20 MHz channel spills into +-2 neighbours
        for (int d = -2; d <= 2; d++) {
            int ch = c->channel + d;
            if (ch < 1 || ch > 13) continue;
            u->spectrum[ch] += v * (d == 0 ? 0.55f : (d == 1 || d == -1) ? 0.3f : 0.12f);
        }
    }
    for (int ch = 0; ch < 14; ch++) u->spectrum[ch] = clampf(u->spectrum[ch], 0, 1);
    u->up_note = used_turn && turn_fresh ? "UP = TURN START" : used_map ? (mv->heading_ok ? "UP = WALKING" : "UP = MAP UP") : "UP = ?";

    if (u->view == VIEW_RADAR) radar_place(u, &G_FULL);
    else radar_place(u, &G_MAIN);
    if (u->view == VIEW_LIST) list_order(u);
}
```

