# Round HUDs, dials and plots, complete

`core/ui_hud.c` and the spring, night and slide parts of `core/ui.c` from OWLOS-AIR-QUALITY-UI (480x320
ESP32, runs on hardware), the dial from OWLOS-THERMO-RELAY `core/ui_main.c` (2.8" CYD, runs on
hardware), and the plot's data pass.

# Air Quality core/ui_hud.c

```c
/* the round HUDs. what a HUD is made of, outside in:
 *   the bezel     a thin ring with a soft halo; a scanner glint runs round it, slow
 *   the ticks     the scale, 270 degrees from lower left over the top
 *   the zones     thin bands in the level colours: where good ends and bad begins
 *   the value     a thick arc, dim at its start and bright at its head (a comet), on a glow,
 *                 the head breathes
 *   inner rings   more channels on the same HUD (the air: TVOC, MQ135, MQ7)
 *   a dashed ring turning slowly the other way: the instrument is alive
 *   the centre    the number, the unit, the level in words
 * everything that moves on its own moves slowly: a HUD on a desk should not fidget */
#include "ui_int.h"

/* --- pieces --- */

void hud_bezel(const ui_t *u, canvas_t *cv, float cx, float cy, float r, uint32_t col, int alive)
{
    if (!gfx_touches(cv, (int)(cy - r - 8), (int)(cy + r + 8))) return;
    const float t = (float)(u->now - u->boot) / 1000.0f;
    const float in = u->intro;
#ifndef AQ_NO_GLASS
    gfx_disc(cv, cx, cy, r - 1.0f, C_GLASS, 64);                       // the glass
    gfx_disc(cv, cx, cy, r * 0.58f, C_BG0, 70);                        // darker inside: depth
#endif
    sector_fx_t glow = { .add = 1 };
    gfx_sector(cv, cx, cy, r + 0.5f, r + 6.0f, 0, 360, col, (int)(26 * in), &glow);
    sector_fx_t scan = { .glint = alive ? fmodf(t * 36.0f, 360.0f) : 0.0f, .glint_w = alive ? 46.0f : 0.0f, .glint_gain = 2.6f };
    // the bezel draws itself in at power on
    gfx_sector(cv, cx, cy, r - 0.8f, r + 0.8f, -90.0f, -90.0f + 360.0f * in, col, 150, &scan);
    if (alive && in >= 1.0f) {                                          // the glint's own light
        float g = fmodf(t * 36.0f, 360.0f) * D2R;
        gfx_glow(cv, cx + r * sinf(g), cy - r * cosf(g), 10.0f, col, 70);
    }
    // the dashed ring, turning the other way
    sector_fx_t dash = { .dashes = 48, .duty = 0.45f, .phase = alive ? -t * 7.0f : 0.0f };
    gfx_sector(cv, cx, cy, r * 0.47f - 0.6f, r * 0.47f + 0.6f, 0, 360, col, (int)(110 * in), &dash);
}

void hud_ticks(const ui_t *u, canvas_t *cv, float cx, float cy, float r, float lo, float hi, float major, int minor,
               int labels, int dec)
{
    if (!gfx_touches(cv, (int)(cy - r - 2), (int)(cy + r + 2)) || hi <= lo || major <= 0.0f) return;
    const float step = minor > 0 ? major / (float)(minor + 1) : major;
    const float first = ceilf(lo / step - 1e-4f) * step;
    int i = 0;
    for (float v = first; v <= hi + step * 1e-3f; v += step, i++) {
        float f = (v - lo) / (hi - lo);
        if (f > u->intro + 1e-3f) break;                               // the ticks come in one by one
        float rem = fmodf(fabsf(v) + major * 1e-3f, major);
        int big = rem < major * 2e-3f;
        float s, c;
        gfx_sincos(hud_deg(f), &s, &c);
        c = -c;
        float r0 = r - (big ? 9.0f : 5.0f);
        gfx_line(cv, cx + r0 * s, cy + r0 * c, cx + (r - 2.0f) * s, cy + (r - 2.0f) * c, big ? 1.6f : 1.0f,
                 big ? C_PRIM : C_QUIET, big ? 230 : 190);
        if (big && labels) {
            char b[12];
            snprintf(b, sizeof b, "%.*f", dec, (double)v);
            float rl = r - 16.0f;
            micro_c(cv, (int)lroundf(cx + rl * s), (int)lroundf(cy + rl * c - 2.0f), b, C_DIM, 230, 1);
        }
    }
}

void hud_track(canvas_t *cv, float cx, float cy, float r0, float r1, uint32_t col, int a)
{
    gfx_sector(cv, cx, cy, r0, r1, HUD_D0, HUD_D0 + HUD_SPAN, col, a, NULL);
}

// a scale value as a fraction of the scale, for the zones of every kind of channel
static float zf(float v, float lo, float hi) { return clampf((v - lo) / (hi - lo), 0.0f, 1.0f); }

void hud_zones(canvas_t *cv, float cx, float cy, float r0, float r1, int ch, float lo, float hi, float base)
{
    const chan_t *c = &CHAN[ch];
    float e[8];
    int lv[8], n = 0;
    (void)base;
    if (c->levels == 1 || c->levels == 3) {                             // 3: the scale is in "times the clean air"
        e[0] = lo; lv[0] = LV_GOOD;
        e[1] = c->lv[0]; lv[1] = LV_FAIR;
        e[2] = c->lv[1]; lv[2] = LV_POOR;
        e[3] = c->lv[2]; lv[3] = LV_BAD;
        e[4] = hi;
        n = 4;
    } else if (c->levels == 2) {
        float s = c->lv[2];
        e[0] = lo;               lv[0] = LV_BAD;
        e[1] = c->lv[0] - 2 * s; lv[1] = LV_POOR;
        e[2] = c->lv[0] - s;     lv[2] = LV_FAIR;
        e[3] = c->lv[0];         lv[3] = LV_GOOD;
        e[4] = c->lv[1];         lv[4] = LV_FAIR;
        e[5] = c->lv[1] + s;     lv[5] = LV_POOR;
        e[6] = c->lv[1] + 2 * s; lv[6] = LV_BAD;
        e[7] = hi;
        n = 7;
    }
    for (int i = 0; i < n; i++) {
        float f0 = zf(e[i], lo, hi), f1 = zf(e[i + 1], lo, hi);
        if (f1 - f0 < 0.002f) continue;
        gfx_sector(cv, cx, cy, r0, r1, hud_deg(f0) + 0.6f, hud_deg(f1) - 0.6f, lv_color(lv[i]), 120, NULL);
    }
}

void hud_arc(const ui_t *u, canvas_t *cv, float cx, float cy, float r0, float r1, float f0, float f1, uint32_t col,
             int glow, int head)
{
    f0 = clampf(f0, 0.0f, 1.0f);
    f1 = clampf(f0 + (clampf(f1, 0.0f, 1.0f) - f0) * u->intro, 0.0f, 1.0f);
    float d0 = hud_deg(f0 < f1 ? f0 : f1), d1 = hud_deg(f0 < f1 ? f1 : f0);
    if (d1 - d0 < 0.5f) d1 = d0 + 0.5f;
    if (!gfx_touches(cv, (int)(cy - r1 - 16), (int)(cy + r1 + 16))) return;
    const float t = (float)(u->now - u->boot) / 1000.0f;
    // the comet: bright at the head, so the head is where it ends, whichever way it ran
    const float dir = f1 < f0 ? -1.0f : 1.0f;
    sector_fx_t halo = { .fade = 0.9f * dir, .add = 1 }, core = { .fade = 0.7f * dir }, edge = { .fade = 0.95f * dir, .add = 1 };
    if (glow) gfx_sector(cv, cx, cy, r0 - 4.0f, r1 + 4.0f, d0, d1, col, 46, &halo);
    gfx_sector(cv, cx, cy, r0, r1, d0, d1, col, 235, &core);
    gfx_sector(cv, cx, cy, r1 - 1.6f, r1, d0, d1, C_FOCUS, 70, &edge);      // a lit rim on the outside
    if (head) {
        float a = (f1 < f0 ? d0 : d1) * D2R, rm = (r0 + r1) * 0.5f;
        float hx = cx + rm * sinf(a), hy = cy - rm * cosf(a);
        float breathe = 0.75f + 0.25f * sinf(t * 2.2f);
        gfx_glow(cv, hx, hy, (r1 - r0) * 1.6f + 6.0f, col, (int)(150 * breathe));
        gfx_disc(cv, hx, hy, (r1 - r0) * 0.32f + 0.8f, C_FOCUS, 255);
    }
}

void hud_needle(canvas_t *cv, float cx, float cy, float r0, float r1, float f, uint32_t col)
{
    float s, c;
    gfx_sincos(hud_deg(f), &s, &c);
    c = -c;
    float px = c, py = -s;                                               // across the needle
    float bx = cx + r0 * s, by = cy + r0 * c, tx = cx + r1 * s, ty = cy + r1 * c;
    gfx_glow(cv, tx, ty, 14.0f, col, 140);
    gfx_tri(cv, tx, ty, bx + px * 5.0f, by + py * 5.0f, bx - px * 5.0f, by - py * 5.0f, col, 240);
    gfx_line(cv, bx, by, tx, ty, 1.0f, C_FOCUS, 200);
}

void hud_marker(canvas_t *cv, float cx, float cy, float r, float f, uint32_t col, int a)
{
    float g = hud_deg(f) * D2R;
    gfx_disc(cv, cx + r * sinf(g), cy - r * cosf(g), 2.2f, col, a);
}

void arrow(canvas_t *cv, float x, float y, float s, int dir, uint32_t c)
{
    if (!dir) { gfx_line(cv, x - s * 0.6f, y, x + s * 0.6f, y, 2.0f, c, 240); return; }
    float d = dir > 0 ? -1.0f : 1.0f;
    gfx_tri(cv, x, y + d * s * 0.6f, x - s * 0.6f, y - d * s * 0.4f, x + s * 0.6f, y - d * s * 0.4f, c, 250);
}

static void cloud(canvas_t *cv, float cx, float cy, float s, uint32_t c, int a)
{
    gfx_disc(cv, cx - s * 0.32f, cy + s * 0.06f, s * 0.26f, c, a);
    gfx_disc(cv, cx + s * 0.02f, cy - s * 0.12f, s * 0.34f, c, a);
    gfx_disc(cv, cx + s * 0.34f, cy + s * 0.08f, s * 0.24f, c, a);
    gfx_fill(cv, (int)(cx - s * 0.32f), (int)(cy + s * 0.06f), (int)(s * 0.66f), (int)(s * 0.26f), c, a);
}

void wx_icon(canvas_t *cv, float cx, float cy, float s, int kind, uint32_t col)
{
    if (kind < 0 || !gfx_touches(cv, (int)(cy - s), (int)(cy + s))) return;
    const uint32_t sun = TH->lv[1], cl = C_FOCUS, wet = C_PRIM;
    if (kind == 0 || kind == 1) {
        float sx = kind ? cx - s * 0.28f : cx, sy = kind ? cy - s * 0.22f : cy, sr = s * (kind ? 0.26f : 0.34f);
        gfx_glow(cv, sx, sy, sr * 2.4f, sun, 120);
        gfx_disc(cv, sx, sy, sr, sun, 250);
        for (int i = 0; i < 8; i++) {
            float a = (float)i * 45.0f * D2R;
            gfx_line(cv, sx + sinf(a) * sr * 1.35f, sy - cosf(a) * sr * 1.35f, sx + sinf(a) * sr * 1.8f,
                     sy - cosf(a) * sr * 1.8f, 1.6f, sun, 230);
        }
        if (kind == 1) cloud(cv, cx + s * 0.12f, cy + s * 0.14f, s * 0.8f, cl, 235);
        return;
    }
    cloud(cv, cx, cy - s * 0.12f, s, kind == 4 ? C_DIM : cl, 235);
    if (kind == 3) {
        for (int i = 0; i < 3; i++) {
            float x = cx - s * 0.3f + (float)i * s * 0.3f;
            gfx_line(cv, x, cy + s * 0.28f, x - s * 0.12f, cy + s * 0.62f, 1.6f, wet, 240);
        }
    } else if (kind == 4) {
        gfx_tri(cv, cx + s * 0.05f, cy + s * 0.1f, cx - s * 0.2f, cy + s * 0.48f, cx + s * 0.02f, cy + s * 0.44f, sun, 250);
        gfx_tri(cv, cx + s * 0.02f, cy + s * 0.38f, cx + s * 0.18f, cy + s * 0.38f, cx - s * 0.08f, cy + s * 0.8f, sun, 250);
    }
    (void)col;
}

/* --- the air: eCO2 on the big arc, TVOC, MQ135 and MQ7 on the rings inside --- */

#define MQ_TOP 4.0f                       // the MQ rings go from clean air to four times that

static float mq_frac(float ratio) { return ratio > 0.0f ? clampf((ratio - 1.0f) / (MQ_TOP - 1.0f), 0.0f, 1.0f) : 0.0f; }

void hud_air(ui_t *u, canvas_t *cv, float cx, float cy, float r, int big)
{
    if (!gfx_touches(cv, (int)(cy - r - 10), (int)(cy + r + 10))) return;
    const aq_now_t *n = &u->snap;
    const int air = n->air;
    const uint32_t ring = air >= LV_POOR ? lv_color(air) : C_PRIM;
    hud_bezel(u, cv, cx, cy, r, ring, 1);
    if (air >= LV_BAD) {                                                // bad air: the halo pulses
        float p = 0.5f + 0.5f * sinf((float)(u->now - u->boot) / 1000.0f * 4.0f);
        sector_fx_t g = { .add = 1 };
        gfx_sector(cv, cx, cy, r + 1.0f, r + 9.0f, 0, 360, ring, (int)(70 * p), &g);
    }
    hud_ticks(u, cv, cx, cy, r, CHAN[CH_CO2].lo, CHAN[CH_CO2].hi, 400.0f, 3, big, 0);
    hud_zones(cv, cx, cy, r - 13.0f, r - 11.0f, CH_CO2, CHAN[CH_CO2].lo, CHAN[CH_CO2].hi, 0);

    // eCO2
    const float r0 = r - 25.0f, r1 = r - 15.0f;
    hud_track(cv, cx, cy, r0, r1, C_QUIET, 60);
    if (n->q[CH_CO2] == Q_OK || n->q[CH_CO2] == Q_LOST) {
        float f = (u->ev[CH_CO2] - CHAN[CH_CO2].lo) / (CHAN[CH_CO2].hi - CHAN[CH_CO2].lo);
        hud_arc(u, cv, cx, cy, r0, r1, 0.0f, f, n->q[CH_CO2] == Q_LOST ? C_DIM : lv_color(n->lv[CH_CO2]), 1, 1);
    } else if (n->q[CH_CO2] == Q_WARM) {                               // warming: the arc fills with the time
        float f = n->warm_s[CH_CO2] ? 1.0f - (float)n->warm_s[CH_CO2] / 1200.0f : 0.0f;
        sector_fx_t d = { .dashes = 90, .duty = 0.5f };
        gfx_sector(cv, cx, cy, r0, r1, HUD_D0, hud_deg(f), C_DIM, 200, &d);
    }
    // the three noses inside
    const int ch[3] = { CH_TVOC, CH_MQ135, CH_MQ7 };
    for (int i = 0; i < 3; i++) {
        float q0 = r - 33.0f - (float)i * 7.0f, q1 = q0 + 4.0f;
        int c = ch[i];
        if (!chan_present(u, c)) continue;
        hud_track(cv, cx, cy, q0, q1, C_QUIET, 55);
        if (n->q[c] != Q_OK && n->q[c] != Q_LOST) continue;
        float f = c == CH_TVOC ? u->ev[c] / CHAN[c].hi : mq_frac(mq_ratio(u, c));
        uint32_t col = n->q[c] == Q_LOST ? C_DIM : n->lv[c] >= 0 ? lv_color(n->lv[c]) : C_PRIM;
        hud_arc(u, cv, cx, cy, q0, q1, 0.0f, fmaxf(f, 0.01f), col, 0, 0);
    }

    // the centre
    char b[24];
    if (n->q[CH_CO2] == Q_OK || n->q[CH_CO2] == Q_LOST) {
        micro_c(cv, (int)cx, (int)cy - 34, "ECO2 PPM", C_DIM, 230, 1);
        snprintf(b, sizeof b, "%d", (int)lroundf(u->ev[CH_CO2]));
        text_c(cv, &FONT_HUD, (int)cx, (int)cy - 26, b, n->q[CH_CO2] == Q_LOST ? C_DIM : C_FOCUS, 255);
        if (n->have_rate[CH_CO2] && fabsf(n->rate[CH_CO2]) >= 30.0f)
            arrow(cv, cx + (float)font_width(&FONT_HUD, b) / 2.0f + 8.0f, cy - 12.0f, 7.0f, n->rate[CH_CO2] > 0 ? 1 : -1,
                  n->rate[CH_CO2] > 0 ? TH->lv[2] : TH->lv[0]);
    } else if (n->q[CH_CO2] == Q_WARM) {
        micro_c(cv, (int)cx, (int)cy - 28, "CCS811", C_DIM, 230, 1);
        micro_c(cv, (int)cx, (int)cy - 18, "WARMING UP", C_FOCUS, 250, 2);
        snprintf(b, sizeof b, "%d:%02d", n->warm_s[CH_CO2] / 60, n->warm_s[CH_CO2] % 60);
        text_c(cv, &FONT_VAL, (int)cx, (int)cy - 4, b, C_PRIM, 240);
    } else {
        micro_c(cv, (int)cx, (int)cy - 18, "NO ECO2", C_DIM, 230, 2);
    }
    const char *word = air >= 0 ? (const char *[]){ "GOOD AIR", "FAIR AIR", "POOR AIR", "BAD AIR" }[air] : n->air_warm ? "" : "...";
    if (*word) text_c(cv, &FONT_VAL, (int)cx, (int)cy + 12, word, air >= 0 ? lv_color(air) : C_DIM, 250);
    if (air >= LV_POOR) micro_c(cv, (int)cx, (int)cy + 34, "OPEN A WINDOW", lv_color(air), 230, 1);

    // the legend, in the gap at the bottom: what the inner rings are
    const char *nm[3] = { "TVOC", "MQ135", "MQ7" };
    for (int i = 0; i < 3; i++) {
        int c = ch[i], y = (int)(cy + r * 0.62f) + i * 9;
        if (!chan_present(u, c)) continue;
        if (n->q[c] != Q_OK && n->q[c] != Q_LOST) snprintf(b, sizeof b, "%s ...", nm[i]);
        else if (c == CH_TVOC) snprintf(b, sizeof b, "%s %d", nm[i], (int)lroundf(u->ev[c]));
        else if (mq_ratio(u, c) > 0.0f) snprintf(b, sizeof b, "%s X%.1f", nm[i], (double)mq_ratio(u, c));
        else snprintf(b, sizeof b, "%s ...", nm[i]);
        uint32_t col = n->q[c] == Q_OK && n->lv[c] >= 0 ? lv_color(n->lv[c]) : C_DIM;
        int w = micro_width(b, 1);
        gfx_disc(cv, cx - (float)w / 2.0f - 5.0f, (float)y + 2.0f, 1.8f, col, 255);
        micro_text(cv, (int)cx - w / 2, y, b, col, 235, 1);
    }
}

/* --- the barometer: the needle is now, the arc is where it came from in 3 hours --- */

static void pres_window(const ui_t *u, float v, float *lo, float *hi, float *major)
{
    if (u->cfg.p_units) { *major = 10.0f; *lo = floorf((v - 25.0f) / 10.0f) * 10.0f; *hi = *lo + 50.0f; }
    else { *major = 10.0f; *lo = floorf((v - 35.0f) / 10.0f) * 10.0f; *hi = *lo + 70.0f; }
}

static const char *tendency(const press_t *p)
{
    if (!p->have) return "TREND IN 3 H";
    if (p->alarm == 2) return p->trend < 0 ? "FALLING FAST" : "RISING FAST";
    if (p->alarm == 1) return p->d3h < 0 ? "FALLING" : "RISING";
    return p->trend < 0 ? "FALLING SLOWLY" : p->trend > 0 ? "RISING SLOWLY" : "STEADY";
}

void hud_pressure(ui_t *u, canvas_t *cv, float cx, float cy, float r, int big)
{
    if (!gfx_touches(cv, (int)(cy - r - 10), (int)(cy + r + 10))) return;
    const aq_now_t *n = &u->snap;
    const press_t *p = &u->pr;
    if (!chan_present(u, CH_PRES)) {                                    // the board on the desk: say what is missing
        hud_bezel(u, cv, cx, cy, r, C_QUIET, 0);
        hud_ticks(u, cv, cx, cy, r, 0.0f, 1.0f, 0.1f, 1, 0, 0);
        micro_c(cv, (int)cx, (int)cy - 34, "PRESSURE", C_DIM, 230, 1);
        micro_c(cv, (int)cx, (int)cy - 22, "NO BAROMETER", C_FOCUS, 250, 2);
        micro_c(cv, (int)cx, (int)cy - 4, "BMP280 SILENT ON I2C", C_DIM, 230, 1);
        micro_c(cv, (int)cx, (int)cy + 8, "PLUG A BMP280 OR BME280", C_PRIM, 230, 1);
        micro_c(cv, (int)cx, (int)cy + 18, "SDA 21  SCL 22  3.3V", C_PRIM, 230, 1);
        micro_c(cv, (int)cx, (int)cy + 30, "FOUND BY ITSELF", C_DIM, 230, 1);
        return;
    }
    const uint32_t acol = p->alarm == 2 ? C_ALARM : p->alarm == 1 ? TH->lv[2] : TH->lv[0];
    hud_bezel(u, cv, cx, cy, r, p->alarm ? acol : C_PRIM, 1);
    if (p->alarm) {                                                     // a fast change: the halo pulses
        float k = 0.5f + 0.5f * sinf((float)(u->now - u->boot) / 1000.0f * (p->alarm == 2 ? 5.0f : 2.5f));
        sector_fx_t g = { .add = 1 };
        gfx_sector(cv, cx, cy, r + 1.0f, r + 10.0f, 0, 360, acol, (int)(80 * k), &g);
    }
    float v = disp_value(u, CH_PRES, u->ev[CH_PRES]), lo, hi, major;
    pres_window(u, v, &lo, &hi, &major);
    hud_ticks(u, cv, cx, cy, r, lo, hi, major, 4, 1, 0);
    float fnow = (v - lo) / (hi - lo);
    const float r0 = r - 32.0f, r1 = r - 23.0f;
    hud_track(cv, cx, cy, r0, r1, C_QUIET, 55);
    // the last 24 hours: their lowest and highest on the scale
    astats_t st;
    aq_stats(&u->hist, 0, CH_PRES, HIST_MIN, &st);
    if (st.n > 30) {
        float flo = (disp_value(u, CH_PRES, st.lo) - lo) / (hi - lo), fhi = (disp_value(u, CH_PRES, st.hi) - lo) / (hi - lo);
        gfx_sector(cv, cx, cy, r0 - 3.0f, r0 - 1.5f, hud_deg(flo), hud_deg(fhi) + 0.5f, C_DIM, 170, NULL);
    }
    if (p->have) {
        float back = disp_value(u, CH_PRES, n->v[CH_PRES] - u->ep);
        hud_arc(u, cv, cx, cy, r0, r1, (back - lo) / (hi - lo), fnow, acol, 1, 0);
        hud_marker(cv, cx, cy, (r0 + r1) * 0.5f, (back - lo) / (hi - lo), C_DIM, 220);
    }
    hud_needle(cv, cx, cy, r - 40.0f, r - 13.0f, fnow, p->alarm ? acol : C_FOCUS);

    char b[32];
    int kind = zambretti_icon(p->zambretti);
    if (kind >= 0) wx_icon(cv, cx, cy - 32.0f, 11.0f, kind, C_FOCUS);
    else micro_c(cv, (int)cx, (int)cy - 34, u->cfg.p_units ? "MMHG" : "HPA", C_DIM, 230, 1);
    snprintf(b, sizeof b, "%d", (int)lroundf(v));
    text_c(cv, &FONT_HUD, (int)cx, (int)cy - 18, b, C_FOCUS, 255);
    if (p->have) {
        float d = u->cfg.p_units ? u->ep * 0.750062f : u->ep;
        snprintf(b, sizeof b, "%+.1f / 3H", (double)d);
        int w = font_width(&FONT_VAL, b);
        arrow(cv, cx - (float)w / 2.0f - 9.0f, cy + 26.0f, 8.0f, p->trend, acol);
        font_text(cv, &FONT_VAL, (int)cx - w / 2, (int)cy + 17, b, acol, 250);
    } else {
        snprintf(b, sizeof b, "%s", u->cfg.p_units ? "MMHG" : "HPA");
        micro_c(cv, (int)cx, (int)cy + 20, b, C_DIM, 230, 2);
    }
    const char *td = tendency(p);
    micro_c(cv, (int)cx, (int)(cy + r * 0.62f), td, p->have ? acol : C_DIM, 240, 1);
    if (big) micro_c(cv, (int)cx, (int)(cy + r * 0.62f) + 10, u->cfg.p_units ? "MMHG" : "HPA", C_DIM, 220, 1);
}

/* --- one channel, for the detail page --- */

void hud_channel(ui_t *u, canvas_t *cv, float cx, float cy, float r, int ch)
{
    if (!gfx_touches(cv, (int)(cy - r - 10), (int)(cy + r + 10))) return;
    if (ch == CH_CO2) { hud_air(u, cv, cx, cy, r, 1); return; }
    if (ch == CH_PRES) { hud_pressure(u, cv, cx, cy, r, 1); return; }
    const aq_now_t *n = &u->snap;
    const chan_t *c = &CHAN[ch];
    const int ok = n->q[ch] == Q_OK || n->q[ch] == Q_LOST;
    const int mq = c->levels == 3;
    const uint32_t col = n->q[ch] == Q_LOST ? C_DIM : n->lv[ch] >= 0 ? lv_color(n->lv[ch]) : C_PRIM;
    hud_bezel(u, cv, cx, cy, r, n->lv[ch] >= LV_POOR ? col : C_PRIM, 1);
    float lo = mq ? 1.0f : disp_value(u, ch, c->lo), hi = mq ? MQ_TOP : disp_value(u, ch, c->hi);
    float major = mq ? 1.0f : ch == CH_TEMP ? (u->cfg.t_units ? 10.0f : 5.0f) : ch == CH_TVOC ? 500.0f : ch == CH_HUM || ch == CH_LIGHT ? 20.0f : 1.0f;
    hud_ticks(u, cv, cx, cy, r, lo, hi, major, mq ? 1 : 4, 1, 0);
    if (c->levels && !u->cfg.t_units) hud_zones(cv, cx, cy, r - 13.0f, r - 11.0f, ch, c->levels == 3 ? 1.0f : c->lo, c->levels == 3 ? MQ_TOP : c->hi, 0);
    const float r0 = r - 34.0f, r1 = r - 22.0f;
    hud_track(cv, cx, cy, r0, r1, C_QUIET, 60);
    float v = mq ? mq_ratio(u, ch) : disp_value(u, ch, u->ev[ch]);
    if (ok && (!mq || v > 0.0f)) hud_arc(u, cv, cx, cy, r0, r1, 0.0f, (v - lo) / (hi - lo), col, 1, 1);

    char b[24];
    micro_c(cv, (int)cx, (int)cy - 34, c->name, C_DIM, 230, 1);
    if (n->q[ch] == Q_WARM) {
        micro_c(cv, (int)cx, (int)cy - 16, "WARMING UP", C_FOCUS, 250, 2);
        snprintf(b, sizeof b, "%d:%02d", n->warm_s[ch] / 60, n->warm_s[ch] % 60);
        text_c(cv, &FONT_VAL, (int)cx, (int)cy, b, C_PRIM, 240);
        return;
    }
    if (!ok) { micro_c(cv, (int)cx, (int)cy - 10, "NO SENSOR", C_DIM, 230, 2); return; }
    if (mq) snprintf(b, sizeof b, v > 0.0f ? "%.2f" : "-", (double)v);
    else fmt_value(u, ch, u->ev[ch], b, sizeof b);
    text_c(cv, &FONT_HUD, (int)cx, (int)cy - 24, b, n->q[ch] == Q_LOST ? C_DIM : C_FOCUS, 255);
    if (mq) {                                                          // times the clean air, and the volts under it
        micro_c(cv, (int)cx, (int)cy + 10, "X CLEAN AIR", C_DIM, 235, 1);
        snprintf(b, sizeof b, "%.3f V", (double)u->ev[ch]);
        micro_c(cv, (int)cx, (int)cy + 19, b, C_DIM, 220, 1);
    } else {
        text_c(cv, &FONT_MEDIUM, (int)cx, (int)cy + 10, unit_of(u, ch), C_DIM, 235);
    }
    if (n->lv[ch] >= 0) micro_c(cv, (int)cx, (int)cy + 30, aq_level_name(ch, n->lv[ch], n->v[ch]), col, 250, 2);
    if (n->q[ch] == Q_LOST) {
        snprintf(b, sizeof b, "LOST %u S AGO", (unsigned)n->age[ch]);
        micro_c(cv, (int)cx, (int)(cy + r * 0.62f), b, C_ALARM, 240, 1);
    }
}

/* --- the little ring on the cards --- */

void mini_ring(ui_t *u, canvas_t *cv, float cx, float cy, float r, int ch)
{
    const aq_now_t *n = &u->snap;
    const chan_t *c = &CHAN[ch];
    sector_fx_t none = { 0 };
    gfx_sector(cv, cx, cy, r - 3.0f, r, HUD_D0, HUD_D0 + HUD_SPAN, C_QUIET, 80, &none);
    if (n->q[ch] != Q_OK && n->q[ch] != Q_LOST) return;
    float f = (u->ev[ch] - c->lo) / (c->hi - c->lo);
    uint32_t col = n->q[ch] == Q_LOST ? C_DIM : n->lv[ch] >= 0 ? lv_color(n->lv[ch]) : C_PRIM;
    hud_arc(u, cv, cx, cy, r - 3.0f, r, 0.0f, fmaxf(f, 0.02f), col, 0, 0);
    float a = hud_deg(clampf(f, 0.0f, 1.0f) * u->intro) * D2R;
    gfx_glow(cv, cx + (r - 1.5f) * sinf(a), cy - (r - 1.5f) * cosf(a), 6.0f, col, 120);
}
```

# Air Quality core/ui_int.h: what the HUD macros are

```c
/* --- the round HUDs (ui_hud.c). scales run 270 degrees, from lower left over the top --- */
#define HUD_D0  (-135.0f)
#define HUD_SPAN 270.0f
static inline float hud_deg(float f) { return HUD_D0 + HUD_SPAN * clampf(f, 0.0f, 1.0f); }
```

```c
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float ease_out(float t) { t = clampf(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
```

# Air Quality core/ui.c: the night palette

```c
/* the night: warm and dark, no blue at all, so the board can stay on in a bedroom */
const theme_t UI_NIGHT = {
    "NIGHT",      RGB(0, 0, 0),    RGB(6, 1, 0),     RGB(16, 4, 0),    RGB(10, 2, 0),
                  RGB(210, 70, 22),  RGB(95, 28, 8),     RGB(255, 150, 80),  RGB(150, 70, 32),
                  { RGB(175, 140, 40), RGB(225, 150, 35), RGB(245, 95, 20), RGB(255, 35, 20) }, RGB(255, 45, 20),
};
```

```c
static uint32_t mixc(uint32_t a, uint32_t b, float t) { return gfx_mix(a, b, (uint32_t)(clampf(t, 0.0f, 1.0f) * 256.0f)); }

static void blend_theme(const theme_t *d, float n)
{
    const theme_t *k = &UI_NIGHT;
    s_mix.name = n > 0.5f ? k->name : d->name;
#define M(f) s_mix.f = mixc(d->f, k->f, n)
    M(bg0); M(bg1); M(glass); M(panel); M(prim); M(quiet); M(focus); M(dim); M(alarm);
    for (int i = 0; i < 4; i++) s_mix.lv[i] = mixc(d->lv[i], k->lv[i], n);
#undef M
    TH = &s_mix;
}
```

# Air Quality core/ui.c: slide, spring, ui_frame

```c
#define SLIDE_MS 280u
static int sliding(const ui_t *u) { return u->scratch_rows > 0 && u->now - u->view_t < SLIDE_MS && u->prev_view != u->view; }

/* a little under critical: a tiny overshoot, alive. integrated in steps of at most 15 ms: with
 * the frame's own dt (200 ms in a slide, two pages drawn) the plain step went unstable and the
 * numbers on screen jumped around - the readings were fine, the spring was not */
static void spring(float *x, float *v, float goal, float dt, float k)
{
    const float c = 2.0f * sqrtf(k) * 0.85f;
    while (dt > 0.0f) {
        float h = dt > 0.015f ? 0.015f : dt;
        *v += (k * (goal - *x) - c * *v) * h;
        *x += *v * h;
        dt -= h;
    }
}

static void prepare_view(ui_t *u, int view)
{
    if (view == VIEW_GRAPHS) graphs_prepare(u);
    else if (view == VIEW_DETAIL) detail_prepare(u);
    else if (view == VIEW_METEO) meteo_prepare(u);
}

void ui_frame(ui_t *u, uint32_t now)
{
    float dt = (float)(now - u->now) / 1000.0f;
    if (dt <= 0.0f || dt > 0.2f) dt = 0.05f;
    u->now = now;
    u->intro = ease_out((float)(now - u->boot) / 1600.0f);

    // night: the light sensor, with a dead band and a few seconds of patience either way
    float goal = u->cfg.night == 2 ? 1.0f : 0.0f;
    if (u->cfg.night == 1 && u->snap.q[CH_LIGHT] == Q_OK) {
        float l = u->snap.v[CH_LIGHT];
        int want = u->night_goal > 0.5f ? (l < 14.0f) : (l < 6.0f);
        if ((float)want != u->night_goal) {
            if (!u->night_since) u->night_since = now ? now : 1;
            if (now - u->night_since > (want ? 8000u : 3000u)) { u->night_goal = (float)want; u->night_since = 0; }
        } else {
            u->night_since = 0;
        }
        goal = u->night_goal;
    }
    u->night += (goal - u->night) * clampf(dt * 0.8f, 0.0f, 1.0f);
    if (fabsf(goal - u->night) < 0.002f) u->night = goal;
    gfx_bright = 256 - (int)(u->night * 140.0f);
    blend_theme(&UI_THEME[u->cfg.theme], u->night);
    uint32_t key = (uint32_t)(u->night * 1000.0f) * 8u + u->cfg.theme;
    if (key != s_bg_key) { build_background(); s_bg_key = key; }

    // the numbers chase the readings
    for (int c = 0; c < CH_N; c++) {
        if (u->snap.q[c] == Q_NONE || (u->snap.q[c] == Q_WARM && !u->snap.age[c] && u->snap.v[c] == 0.0f)) continue;
        float v = u->snap.v[c];
        if (u->ev[c] < -1e8f) { u->ev[c] = v; u->evv[c] = 0.0f; continue; }
        spring(&u->ev[c], &u->evv[c], v, dt, 40.0f);
    }
    aq_pressure(&u->snap, &u->hist, (float)u->cfg.alt_m, u->cfg.p_alert, &u->pr);
    spring(&u->ep, &u->epv, u->pr.have ? u->pr.d3h : 0.0f, dt, 30.0f);
    int tab = tab_of(u->view);
    if (tab >= 0) u->tab_x += ((float)tab - u->tab_x) * clampf(dt * 12.0f, 0.0f, 1.0f);
    if (u->cursor >= 0 && !u->t_down && now - u->cursor_t > 10000u) u->cursor = -1;

    prepare_view(u, u->view);
    if (sliding(u)) prepare_view(u, u->prev_view);
}
```

# Air Quality core/ui.c: the slide between pages in ui_strip, and the tab bar

```c
static void tab_bar(ui_t *u, canvas_t *cv)
{
    if (!gfx_touches(cv, TAB_Y - 2, GFX_H)) return;
    static const char *const T[UI_TABS] = { "AIR", "METEO", "GRAPHS", "SENSORS", "SETUP" };
    gfx_fill(cv, 0, TAB_Y, GFX_W, TAB_H, C_PANEL, 215);
    gfx_hline(cv, 0, GFX_W - 1, TAB_Y, C_QUIET, 230);
    const int w = GFX_W / UI_TABS;
    int cur = tab_of(u->view);
    for (int i = 0; i < UI_TABS; i++) {
        int on = i == cur, pr = pressed(u, HIT_TAB + i);
        uint32_t c = on ? C_FOCUS : C_PRIM;
        if (pr) gfx_fill(cv, i * w + 2, TAB_Y + 2, w - 4, TAB_H - 3, C_PRIM, 70);
        micro_c(cv, i * w + w / 2, TAB_Y + 9, T[i], c, on ? 255 : 200, 2);
        if (i) gfx_vline(cv, i * w, TAB_Y + 6, TAB_Y + TAB_H - 6, C_QUIET, 160);
    }
    // the marker slides from tab to tab, with a glow
    float mx = u->tab_x * (float)w + (float)w / 2.0f;
    gfx_fill(cv, (int)(mx - w * 0.32f), TAB_Y + 1, (int)(w * 0.64f), 2, C_FOCUS, 255);
    gfx_glow(cv, mx, (float)TAB_Y + 2.0f, 26.0f, C_PRIM, 90);
    // something needs looking at: a dot on its tab
    if (u->pr.alarm) gfx_disc(cv, (float)(w + w - 14), (float)TAB_Y + 8.0f, 3.0f, C_ALARM, 255);
    if (u->snap.air >= LV_POOR) gfx_disc(cv, (float)(w - 14), (float)TAB_Y + 8.0f, 3.0f, lv_color(u->snap.air), 255);
}
```

```c
void ui_strip(const ui_t *u_in, canvas_t *cv, uint32_t *scratch)
{
    ui_t *u = (ui_t *)u_in;                                      // the pages take a plain pointer; nothing here writes to it
    if (sliding(u) && scratch && cv->h <= u->scratch_rows) {
        // the old page goes out one way, the new one comes in after it. put together in the old
        // page's rows, then copied over: no buffer shared between the two cores
        canvas_t old = { scratch, cv->y0, cv->h };
        draw_view(u, &old, u->prev_view);
        draw_view(u, cv, u->view);
        float t = ease_out((float)(u->now - u->view_t) / (float)SLIDE_MS);
        int s = (int)(t * GFX_W);
        for (int r = 0; r < cv->h; r++) {
            uint32_t *n = cv->px + r * GFX_W, *o = old.px + r * GFX_W;
            if (u->slide_dir > 0) {                              // new from the right
                memmove(o, o + s, (size_t)(GFX_W - s) * 4);
                memcpy(o + GFX_W - s, n, (size_t)s * 4);
            } else {                                             // new from the left
                memmove(o + s, o, (size_t)(GFX_W - s) * 4);
                memcpy(o, n + GFX_W - s, (size_t)s * 4);
            }
            memcpy(n, o, GFX_W * 4);
        }
        int seam = u->slide_dir > 0 ? GFX_W - s : s;
        gfx_vline(cv, seam, 0, TAB_Y - 1, C_FOCUS, 200);
        for (int r = 0; r < cv->h; r++) {
            uint32_t *row = cv->px + r * GFX_W;
            for (int k = 1; k < 10; k++) {
                int a = (10 - k) * 9;
                if (seam - k >= 0) row[seam - k] = gfx_add(row[seam - k], gfx_scale(C_PRIM, (uint32_t)a));
                if (seam + k < GFX_W) row[seam + k] = gfx_add(row[seam + k], gfx_scale(C_PRIM, (uint32_t)a));
            }
        }
    } else {
        draw_view(u, cv, u->view);
    }
    uint32_t t4 = ui_clock_us ? ui_clock_us() : 0;
    if (u->view != VIEW_CAL) tab_bar(u, cv);
    if (ui_clock_us) ui_prof[4] += ui_clock_us() - t4;
    if ((int32_t)(u->toast_until - u->now) > 0) {
        int w = micro_width(u->toast, 2) + 20;
        int ty = GFX_H / 2 - 16;
        chamfer_box(cv, (GFX_W - w) / 2, ty, w, 28, C_BG0, 230, C_FOCUS, 230);
        micro_c(cv, GFX_W / 2, ty + 9, u->toast, C_FOCUS, 255, 2);
    }
    if (u->t_down && u->view != VIEW_CAL) {                      // where the touch lands: a bad alignment shows at once
        gfx_ring(cv, (float)u->t_x, (float)u->t_y, 10.0f, 1.2f, C_FOCUS, 210);
        gfx_disc(cv, (float)u->t_x, (float)u->t_y, 1.8f, C_FOCUS, 255);
    }
}
```

# Thermo Relay core/ui_main.c: the dial

```c
void main_frame(ui_t *u)
{
    /* the scale stays put while the target moves in its middle 60%, so the numbers don't
     * slide under the finger. it jumps only when the target gets near an end */
    float span = SPAN(u), st = LSTEP(u);
    float set = to_units(u->cfg.set, u->cfg.units);
    float lo = u->scale_lo;
    if (lo < -900.0f || set < lo + span * 0.2f || set > lo + span * 0.8f) lo = roundf((set - span * 0.5f) / st) * st;
    u->scale_lo = lo;
    plot_prepare(u, &SPARK_PLOT);
}

static float ang(const ui_t *u, float v)                 // display units -> degrees, 0 = up
{
    return clampf(-135.0f + 270.0f * (v - u->scale_lo) / SPAN(u), -141.0f, 141.0f);
}

static float ring_x(float a, float r) { return DCX + r * sinf(a * D2R); }
static float ring_y(float a, float r) { return DCY - r * cosf(a * D2R); }
```

```c
/* --- what the relay does and why, in words --- */

typedef struct { const char *word; uint32_t col; char detail[48]; int alarm; } state_t;

static void state_of(const ui_t *u, state_t *s)
{
    const thermo_t *th = &u->snap;
    const thermo_cfg_t *c = &u->cfg;
    char t[16], d[16];
    s->detail[0] = 0;
    s->alarm = 0;
    if (th->fault) {
        s->alarm = 1;
        s->col = C_ALARM;
        if (th->fault == FAULT_PROBE) s->word = "NO PROBE";
        else if (th->fault == FAULT_NORISE) s->word = c->mode == MODE_COOL ? "NO DROP" : "NO RISE";
        else s->word = c->mode == MODE_COOL ? "TOO COLD" : "TOO HOT";
        if (th->fault == FAULT_LIMIT) {
            fmt_temp(t, sizeof t, c->mode == MODE_COOL ? c->limit_lo + 1.0f : c->limit_hi - 1.0f, c->units, 0);
            snprintf(s->detail, sizeof s->detail, "relay off till %s", t);
        } else {
            snprintf(s->detail, sizeof s->detail, "relay off \xC2\xB7 tap for help");
        }
        return;
    }
    if (c->mode == MODE_OFF) {
        s->word = "OFF";
        s->col = C_DIM;
        snprintf(s->detail, sizeof s->detail, "relay stays off");
        return;
    }
    int cool = c->mode == MODE_COOL;
    float rate = th->have_rate ? th->rate : 0.0f;
    if (th->relay) {
        s->word = cool ? "COOLING" : "HEATING";
        s->col = cool ? C_COOL : C_HEAT;
        if (!th->want) {                                       // done, but the minimum run isn't over
            fmt_dur(d, sizeof d, (uint32_t)th->hold_s);
            snprintf(s->detail, sizeof s->detail, "min run, stops in %s", d);
            return;
        }
        float off = thermo_off_at(c);
        fmt_temp(t, sizeof t, off, c->units, 0);
        float eta = cool ? (th->t - off) / -rate : (off - th->t) / rate;   // hours, if it keeps going like now
        if (th->have_rate && eta > 0.016f && eta < 10.0f && (cool ? rate < -0.05f : rate > 0.05f))
            snprintf(s->detail, sizeof s->detail, "off at %s \xC2\xB7 ~%d min", t, (int)lroundf(eta * 60.0f));
        else
            snprintf(s->detail, sizeof s->detail, "off at %s", t);
        return;
    }
    if (th->want && th->hold_s > 0) {                          // wants to run, the relay rests
        s->word = "WAITING";
        s->col = C_FOCUS;
        fmt_dur(d, sizeof d, (uint32_t)th->hold_s);
        snprintf(s->detail, sizeof s->detail, "relay rest, starts in %s", d);
        return;
    }
    s->word = "IDLE";
    s->col = C_DIM;
    float on = thermo_on_at(c);
    fmt_temp(t, sizeof t, on, c->units, 0);
    float eta = cool ? (on - th->t) / rate : (th->t - on) / -rate;
    if (th->have_t && th->have_rate && eta > 0.016f && eta < 10.0f && (cool ? rate > 0.05f : rate < -0.05f))
        snprintf(s->detail, sizeof s->detail, "on at %s \xC2\xB7 ~%d min", t, (int)lroundf(eta * 60.0f));
    else
        snprintf(s->detail, sizeof s->detail, "on at %s", t);
}
```

```c
static void dial(ui_t *u, canvas_t *cv)
{
    if (!gfx_touches(cv, DCY - DR - 16, DCY + DR + 16)) return;
    const thermo_t *th = &u->snap;
    const thermo_cfg_t *c = &u->cfg;
    const int un = c->units, off = c->mode == MODE_OFF;
    const float span = SPAN(u), lst = LSTEP(u), lo = u->scale_lo;
    const float set = to_units(c->set, un), band = delta_units(c->band, un);
    char buf[16];

    gfx_disc(cv, DCX, DCY, DR - 5, C_GLASS, 46);
    gfx_ring(cv, DCX, DCY, DR - 5, 1.0f, C_QUIET, 70);

    // ticks every 1 C (2 F), longer at the numbers. numbers outside, but not under the target notch
    float tst = un ? 2.0f : 1.0f, sa = ang(u, set);
    for (float v = lo; v <= lo + span + 0.01f; v += tst) {
        float a = ang(u, v);
        int major = fabsf(fmodf(v - lo, lst)) < 0.01f;
        float r0 = major ? DR - 13.0f : DR - 9.0f, r1 = DR - 4.0f;
        gfx_line(cv, ring_x(a, r0), ring_y(a, r0), ring_x(a, r1), ring_y(a, r1), major ? 1.4f : 1.0f,
                 C_QUIET, major ? 240 : 150);
        if (major && fabsf(a - sa) > 10.0f) {
            snprintf(buf, sizeof buf, "%d", (int)lroundf(v));
            int w = micro_width(buf, 1);
            micro_text(cv, (int)lroundf(ring_x(a, DR + 9.0f)) - w / 2, (int)lroundf(ring_y(a, DR + 9.0f)) - 2, buf, C_DIM, 235, 1);
        }
    }
    gfx_arc(cv, DCX, DCY, DR, -135.0f, 135.0f, 1.2f, C_QUIET, 210);

    if (!off) gfx_arc(cv, DCX, DCY, DR, ang(u, set - band * 0.5f), ang(u, set + band * 0.5f), 7.0f, C_PRIM, 75);

    if (th->have_t) {
        float ta = ang(u, to_units(th->t, un));
        int cool = c->mode == MODE_COOL;
        uint32_t col = cool ? C_COOL : C_HEAT;
        if (th->relay && !th->fault) {
            // running: the arc from the room to where it stops, light flowing along it
            float oa = ang(u, to_units(thermo_off_at(c), un));
            if (fabsf(oa - ta) > 0.5f) {
                gfx_arc(cv, DCX, DCY, DR, ta, oa, 4.0f, col, 235);
                float dir = oa > ta ? 1.0f : -1.0f, len = fabsf(oa - ta);
                float ph = fmodf((float)u->now * 0.012f, 9.0f);       // 12 degrees a second
                for (float d = ph; d < len; d += 9.0f)
                    gfx_disc(cv, ring_x(ta + dir * d, DR), ring_y(ta + dir * d, DR), 1.3f, C_FOCUS, 210);
            }
        } else if (!off) {
            gfx_arc(cv, DCX, DCY, DR, ta, sa, 1.4f, C_DIM, 120);    // how far from the target
        }
        // the room: the boldest mark, it is the one that moves
        uint32_t g = th->fault ? C_ALARM : th->relay ? col : C_PRIM;
        float x = ring_x(ta, DR), y = ring_y(ta, DR);
        gfx_glow(cv, x, y, 16.0f, g, 170);
        gfx_disc(cv, x, y, 5.0f, g, 255);
        gfx_disc(cv, x, y, 2.6f, C_FOCUS, 255);
    }

    // the target notch, across the ring, and a pointer outside it
    uint32_t sc = off ? C_DIM : C_FOCUS;
    gfx_line(cv, ring_x(sa, DR - 9.0f), ring_y(sa, DR - 9.0f), ring_x(sa, DR + 5.0f), ring_y(sa, DR + 5.0f), 2.6f, sc, 250);
    float pa = (sa - 6.0f) * D2R, pb = (sa + 6.0f) * D2R, ro = DR + 14.0f;
    gfx_tri(cv, ring_x(sa, DR + 6.0f), ring_y(sa, DR + 6.0f),
            DCX + ro * sinf(pa), DCY - ro * cosf(pa), DCX + ro * sinf(pb), DCY - ro * cosf(pb), sc, 250);
}

static void centre(ui_t *u, canvas_t *cv)
{
    if (!gfx_touches(cv, DCY - 50, DCY + 48)) return;
    const thermo_t *th = &u->snap;
    const int un = u->cfg.units;
    const char *lbl = u->cfg.sim ? "ROOM (SIMULATED)" : "ROOM";
    micro_text(cv, DCX - micro_width(lbl, 1) / 2, DCY - 47, lbl, C_QUIET, 235, 1);
    const char *deg = un ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
    int y = DCY - 36;
    if (!th->have_t) {                                 // no reading: no number, never an old one
        int w = font_width(&FONT_BIG, "--.-") + 3 + font_width(&FONT_BIG, deg);
        int x = DCX - w / 2;
        x += font_text(cv, &FONT_BIG, x, DCY - 14, "--.-", C_DIM, 220);
        font_text(cv, &FONT_BIG, x + 3, DCY - 14, deg, C_DIM, 200);
        return;
    }
    float v = roundf(to_units(th->t, un) * 10.0f) / 10.0f;
    int neg = v < 0.0f;
    float a = fabsf(v);
    int ip = (int)a, dp = (int)lroundf((a - (float)ip) * 10.0f);
    if (dp >= 10) { ip++; dp = 0; }
    char si[16], sd[16];
    snprintf(si, sizeof si, "%s%d", neg && (ip || dp) ? "-" : "", ip);
    snprintf(sd, sizeof sd, ".%d", dp);
    int wi = font_width(&FONT_TEMP, si), wd = font_width(&FONT_BIG, sd), wu = font_width(&FONT_BIG, deg);
    int wr = wd > wu ? wd : wu;
    int show_dec = wi + 3 + wr <= 156;                 // "-18.4" won't fit, then it is "-18"
    int w = wi + 3 + wr, x = DCX - w / 2;
    uint32_t col = th->fault ? C_ALARM : C_FOCUS;
    font_text(cv, &FONT_TEMP, x, y, si, col, 255);
    font_text(cv, &FONT_BIG, x + wi + 3, y + 2, deg, C_DIM, 235);           // degree sign up, like a superscript
    if (show_dec) font_text(cv, &FONT_BIG, x + wi + 3, y + 36, sd, col, 255);   // decimal on the big digits' baseline

    // trend under it: arrow and degrees an hour
    if (th->have_rate) {
        float r = delta_units(th->rate, un);
        char buf[24];
        if (fabsf(r) < 0.1f) {
            snprintf(buf, sizeof buf, "steady");
            font_text(cv, &FONT_MEDIUM, DCX - font_width(&FONT_MEDIUM, buf) / 2, DCY + 31, buf, C_DIM, 230);
        } else {
            snprintf(buf, sizeof buf, "%.1f\xC2\xB0/h", (double)fabsf(r));
            int tw = font_width(&FONT_MEDIUM, buf), tx = DCX - (tw + 12) / 2 + 12;
            float ax = (float)(tx - 7), ay = (float)(DCY + 38);
            if (r > 0) gfx_tri(cv, ax - 4, ay + 3, ax + 4, ay + 3, ax, ay - 4, C_DIM, 240);
            else gfx_tri(cv, ax - 4, ay - 3, ax + 4, ay - 3, ax, ay + 4, C_DIM, 240);
            font_text(cv, &FONT_MEDIUM, tx, DCY + 31, buf, C_DIM, 230);
        }
    }
}

static void status_line(ui_t *u, canvas_t *cv)
{
    if (!gfx_touches(cv, 168, 202)) return;
    state_t s;
    state_of(u, &s);
    // an alarm pulses (a blink would hide the word half of the time)
    int a = s.alarm ? 150 + (int)(105.0f * (0.5f + 0.5f * sinf((float)u->now * 0.0063f))) : 255;
    int w = micro_width(s.word, 2);
    if (s.alarm) gfx_glow(cv, DCX, 176, 40.0f, s.col, (a - 150) / 2);
    micro_text(cv, DCX - w / 2, 171, s.word, s.col, a, 2);
    int dw = font_width(&FONT_MEDIUM, s.detail);
    font_text(cv, &FONT_MEDIUM, DCX - dw / 2, 185, s.detail, s.alarm ? C_ALARM : C_DIM, 235);
}
```

# Air Quality core/ui_graph.c: a plot worked out once a frame

```c
/* graphs: the history plots, the GRAPHS page (every channel, one line each) and the DETAIL page
 * (one channel: its HUD and a big plot).
 * a plot is worked out once a frame into per-column numbers, the strips only paint them. what is
 * drawn, back to front:
 *   min..max     zoomed out a column holds many points: the band from the lowest to the highest
 *   the glow     under the curve, fading to the bottom
 *   the curve    the mean of each column, coloured by its level where the channel has levels
 *   power on     a dashed line: the board was off before it, for how long nobody knows (no clock)
 *   now          a dot with a glow at the right end */
#include "ui_int.h"

#define PW_MAX 336
typedef struct {
    plot_t  p;
    int     ch, range, big, ok;
    float   ppx;                       /* points per column */
    float   lo, hi, step;              /* display units */
    int16_t mean[PW_MAX], mn[PW_MAX], mx[PW_MAX];
    uint8_t mark[PW_MAX];              /* a power-on lies in this column */
    int     have, first_x, last_x;
    astats_t st;
} slot_t;
static slot_t S[9];

static float dv(const ui_t *u, int ch, int16_t raw) { return disp_value(u, ch, aq_hv(ch, raw)); }

static float min_span(const ui_t *u, int ch)
{
    switch (ch) {
    case CH_CO2:  return 100.0f;
    case CH_TVOC: return 50.0f;
    case CH_TEMP: return u->cfg.t_units ? 2.0f : 1.0f;
    case CH_HUM:  return 5.0f;
    case CH_PRES: return u->cfg.p_units ? 2.0f : 3.0f;
    case CH_LIGHT: return 10.0f;
    default:      return 0.05f;
    }
}

static float nice_step(float span, int lines)
{
    static const float N[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
    for (int i = 0; i < 16; i++) if (span / N[i] <= (float)lines) return N[i];
    return 2000.0f;
}

void plot_prepare(ui_t *u, int slot, int ch, int range, const plot_t *p, int big)
{
    slot_t *s = &S[slot];
    s->p = *p;
    if (s->p.x1 - s->p.x0 + 1 > PW_MAX) s->p.x1 = s->p.x0 + PW_MAX - 1;
    s->ch = ch;
    s->range = range;
    s->big = big;
    s->have = 0;
    s->first_x = s->last_x = -1;
    const int week = range_week(range), points = range_points(range), w = s->p.x1 - s->p.x0 + 1;
    s->ppx = (float)points / (float)w;
    float mn = 1e9f, mx = -1e9f;
    hpt_t q;
    for (int i = 0; i < w; i++) {
        float a0 = (float)(w - 1 - i) * s->ppx, a1 = a0 + s->ppx;
        int i0, i1;
        if (s->ppx < 1.0f) { i0 = (int)(a0 + s->ppx * 0.5f); i1 = i0 + 1; }
        else { i0 = (int)a0; i1 = (int)ceilf(a1); }
        s->mean[i] = s->mn[i] = s->mx[i] = HIST_NONE;
        s->mark[i] = 0;
        float sum = 0;
        int n = 0;
        int16_t lo = INT16_MAX, hi = INT16_MIN;
        for (int a = i0; a < i1; a++) {
            if (!aq_point(&u->hist, week, a, &q)) break;
            if (q.flags & HF_BOOT) s->mark[i] = 1;
            if (q.v[ch] == HIST_NONE) continue;
            sum += (float)q.v[ch];
            n++;
            if (q.v[ch] < lo) lo = q.v[ch];
            if (q.v[ch] > hi) hi = q.v[ch];
        }
        if (!n) continue;
        float m = sum / (float)n;
        if (s->ppx < 1.0f && aq_point(&u->hist, week, i0 + 1, &q) && q.v[ch] != HIST_NONE) {
            float f = a0 + s->ppx * 0.5f - (float)i0;            // between two points: a straight line
            m += ((float)q.v[ch] - m) * (1.0f - f);
        }
        s->mean[i] = (int16_t)lroundf(m);
        s->mn[i] = lo;
        s->mx[i] = hi;
        s->have = 1;
        if (s->first_x < 0) s->first_x = s->p.x0 + i;
        s->last_x = s->p.x0 + i;
        float vlo = dv(u, ch, lo), vhi = dv(u, ch, hi);
        if (vlo < mn) mn = vlo;
        if (vhi > mx) mx = vhi;
    }
    if (mn > mx) {                                               // nothing yet: around now
        float v = disp_value(u, ch, u->ev[ch] > -1e8f ? u->ev[ch] : CHAN[ch].lo);
        mn = mx = v;
    }
    float span = min_span(u, ch);
    if (mx - mn < span) { float c = (mx + mn) * 0.5f; mn = c - span * 0.5f; mx = c + span * 0.5f; }
    float pad = (mx - mn) * (big ? 0.08f : 0.12f);
    mn -= pad;
    mx += pad;
    if (big) {
        s->step = nice_step(mx - mn, 4);
        s->lo = floorf(mn / s->step) * s->step;
        s->hi = ceilf(mx / s->step) * s->step;
    } else {
        s->step = 0;
        s->lo = mn;
        s->hi = mx;
    }
    aq_stats(&u->hist, week, ch, points, &s->st);
    s->ok = 1;
}
```

