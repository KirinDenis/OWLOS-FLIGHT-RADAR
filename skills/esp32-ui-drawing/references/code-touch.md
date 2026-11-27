# Touch, keyboard and alignment, complete

Runs on hardware on the 2.8" CYD (XPT2046), the 3.5" CYD (XPT2046 on the LCD bus), the round C3
(CST816) and the Air Quality board (XPT2046). FT6336 and GT911 paths are built, not run here.

# Flight Radar core/ui.c: the alignment and the touch state machine

```c
/* --- touch alignment: 5 crosses, the median of each press, least squares (the radar's) --- */

static int solve3(float m[3][4], float *x)
{
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabsf(m[r][c]) > fabsf(m[p][c])) p = r;
        if (fabsf(m[p][c]) < 1e-6f) return 0;
        if (p != c) for (int j = 0; j < 4; j++) { float t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            float f = m[r][c] / m[c][c];
            for (int j = c; j < 4; j++) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 3; i++) x[i] = m[i][3] / m[i][i];
    return 1;
}

void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y)
{
    *x = (int)lroundf(c->a * rx + c->b * ry + c->c);
    *y = (int)lroundf(c->d * rx + c->e * ry + c->f);
}

void ui_start_calibration(ui_t *u)
{
    u->view = VIEW_CAL;
    u->cal_step = 0;
    u->cal_n = 0;
    u->t_down = 0;
    u->cal_wait = 1;
}

static int median16(const int16_t *v, int n)
{
    int16_t s[48];
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) {
        int16_t x = v[i];
        int j = i - 1;
        while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
        s[j + 1] = x;
    }
    return n ? s[n / 2] : 0;
}

static void cal_touch(ui_t *u, int down, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->cal_wait) { if (!down) u->cal_wait = 0; return; }
    if (down) {
        if (!u->t_down) { u->t_down = 1; u->t_since = now; u->cal_n = 0; }
        if (now - u->t_since > 120u && u->cal_n < 48) {
            u->cal_sx[u->cal_n] = (int16_t)rx;
            u->cal_sy[u->cal_n] = (int16_t)ry;
            u->cal_n++;
        }
        return;
    }
    if (!u->t_down) return;
    u->t_down = 0;
    if (u->cal_n < 2 || now - u->t_since < 300u) return;
    int n = u->cal_n > 4 ? u->cal_n - 2 : u->cal_n;
    u->cal_raw[u->cal_step][0] = (float)median16(u->cal_sx, n);
    u->cal_raw[u->cal_step][1] = (float)median16(u->cal_sy, n);
    if (++u->cal_step < CAL_POINTS) return;
    float mx[3][4] = { { 0 } }, my[3][4] = { { 0 } }, X[3], Y[3];
    for (int i = 0; i < CAL_POINTS; i++) {
        float f[3] = { u->cal_raw[i][0], u->cal_raw[i][1], 1.0f };
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) { mx[r][c] += f[r] * f[c]; my[r][c] += f[r] * f[c]; }
            mx[r][3] += f[r] * (float)CAL_PT[i][0];
            my[r][3] += f[r] * (float)CAL_PT[i][1];
        }
    }
    int ok = solve3(mx, X) && solve3(my, Y);
    float worst = 0;
    if (ok) {
        for (int i = 0; i < CAL_POINTS; i++) {
            float ex = X[0] * u->cal_raw[i][0] + X[1] * u->cal_raw[i][1] + X[2] - (float)CAL_PT[i][0];
            float ey = Y[0] * u->cal_raw[i][0] + Y[1] * u->cal_raw[i][1] + Y[2] - (float)CAL_PT[i][1];
            float e = sqrtf(ex * ex + ey * ey);
            if (e > worst) worst = e;
        }
    }
    u->cal_err = worst;
    if (!ok || worst > 18.0f) {
        toast(u, "TRY AGAIN");
        u->cal_step = 0;
        return;
    }
    u->cal = (touch_cal_t){ X[0], X[1], X[2], Y[0], Y[1], Y[2] };
    cmd->save_cal = 1;
    u->view = VIEW_MAIN;
    toast(u, "TOUCH ALIGNED");
}
```

```c
static int hit_at(const ui_t *u, int x, int y)
{
    switch (u->view) {
    case VIEW_MAIN:  return main_hit(u, x, y);
    case VIEW_LIST:  return list_hit(u, x, y);
    case VIEW_SETUP: return setup_hit(u, x, y);
    case VIEW_WIFI:  return wifi_hit(u, x, y);
    case VIEW_KEYS:  return keys_hit(u, x, y);
    case VIEW_ABOUT: return about_hit(u, x, y);
    default:         return HIT_NONE;
    }
}

static int repeats(const ui_t *u, int hit)
{
    return (u->view == VIEW_SETUP && setup_repeats(hit)) || (u->view == VIEW_KEYS && keys_repeats(u, hit));
}

static void select_plane(ui_t *u, int i)
{
    u->sel = i >= 0 ? u->snap.p[i].icao : 0;
}
```

```c
static void act(ui_t *u, int hit, int longp, int x, int y, ui_cmd_t *cmd)
{
    if (u->view == VIEW_SETUP) { setup_act(u, hit, longp, cmd); return; }
    if (longp) return;
    if (u->view == VIEW_WIFI) { wifi_act(u, hit, cmd); return; }
    if (u->view == VIEW_KEYS) { keys_act(u, hit, cmd); return; }
    if (u->view == VIEW_ABOUT) { about_act(u, hit); return; }
    if (u->view == VIEW_MAIN) {
        if (hit == HIT_WIFI) { wifi_open(u, cmd); return; }
        if (hit == HIT_NEWS) { about_open(u); return; }
        if (hit == HIT_SCOPE) {
            int i = scope_pick(u, x, y);
#if RADAR_ROUND
            // round glass, no panel: a tap on nothing brings the HUE LIST SET row, another one hides it
            if (i < 0 && !u->sel) { u->menu_until = (int32_t)(u->menu_until - u->now) > 0 ? 0 : u->now + ROUND_MENU_MS; return; }
            u->menu_until = 0;
#endif
            select_plane(u, i);
            return;
        }
        if (hit >= HIT_ROW && hit < HIT_ROW + SKY_MAX) { select_plane(u, u->order[hit - HIT_ROW]); return; }
        if (hit == HIT_LOOK || hit == HIT_LOOK_BACK) {   // the button's right half on, its left half back (0.17)
            set_look(u, cmd, hit == HIT_LOOK ? ui_look_after(u->cfg.look) : ui_look_before(u->cfg.look));
            if (RADAR_ROUND) u->menu_until = u->now + ROUND_MENU_MS;   // stays up for the next look
            return;
        }
        if (hit == HIT_DOTS) {                         // the square map's panel: in, or out again
            u->panel_open ^= 1;
            if (!u->panel_open) u->sel = 0;
            return;
        }
        if ((hit == HIT_BTN + 0 || hit == HIT_PANEL) && u->sel) {   // picked: the next nearest (round: its card)
            int at = 0;
            for (int k = 0; k < u->n_order; k++) if (u->snap.p[u->order[k]].icao == u->sel) at = k;
            if (u->n_order) select_plane(u, u->order[(at + 1) % u->n_order]);
        } else if (hit == HIT_BTN + 0) {
            next_theme(u, cmd);
            if (RADAR_ROUND) u->menu_until = u->now + ROUND_MENU_MS;   // stays up for the next colours
        } else if (hit == HIT_BTN + 1 && u->sel && !RADAR_ROUND) {    // picked: WATCH (round: its row has no WATCH)
            const plane_t *p = sel_plane(u);
            if (p) watch_toggle(u, p, cmd);
        } else if (hit == HIT_BTN + 1) {
            u->menu_until = 0;
            u->view = VIEW_LIST;
            u->list_scroll = 0;
        } else if (hit == HIT_BTN + 2) {
            u->menu_until = 0;
            if (u->sel) u->sel = 0;
            else { u->view = VIEW_SETUP; u->setup_scroll = 0; }
        }
        return;
    }
    if (u->view == VIEW_LIST) {
        if (hit >= HIT_ROW && hit < HIT_ROW + SKY_MAX) { select_plane(u, u->order[hit - HIT_ROW]); u->view = VIEW_MAIN; }
        else if (hit == HIT_BAR + 0) next_range(u, cmd);
        else if (hit == HIT_BAR + 1) u->view = VIEW_MAIN;
    }
}

void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { cal_touch(u, down, rx, ry, now, cmd); return; }
    if (down && !u->t_down) {
        u->t_down = 1;
        u->t_x0 = u->t_x = x;
        u->t_y0 = u->t_y = y;
        u->t_since = now;
        u->t_moved = u->t_long = u->t_ns = u->t_reps = u->t_rpt = u->t_turn = 0;
        u->t_hit = hit_at(u, x, y);
        u->t_scroll0 = u->view == VIEW_LIST ? u->list_scroll : u->view == VIEW_WIFI ? u->wifi_scroll : u->setup_scroll;
    }
    if (down) {
        u->t_x = x;
        u->t_y = y;
        if (u->t_ns < 16) { u->t_sx[u->t_ns] = (int16_t)x; u->t_sy[u->t_ns] = (int16_t)y; u->t_ns++; }
        if (u->t_ns == 3) {                            // the first readings of a resistive press wander
            u->t_x0 = median16(u->t_sx, 3);
            u->t_y0 = median16(u->t_sy, 3);
            u->t_hit = hit_at(u, u->t_x0, u->t_y0);
            if (repeats(u, u->t_hit)) { u->t_rpt = 1; u->t_rep = now + 450u; }
            if (u->view == VIEW_MAIN && u->t_hit == HIT_SCOPE) {
                /* the scope under a finger: on the rim it turns the map, inside it zooms. one finger
                 * pinches against home: what was under the finger stays under it */
                float dx = (float)(u->t_x0 - view_cx(u)), dy = (float)(SCY - u->t_y0), r = sqrtf(dx * dx + dy * dy);
                u->t_turn = r >= PR - 4 && !square_look(u) ? 1 : r >= 10 ? 2 : 0;   // the flat map has no rim: north stays up
                u->t_a0 = atan2f(dx, dy) / D2R;
                u->t_r0 = r;
                u->t_up0 = (float)u->cfg.up;
                u->t_range0 = u->cfg.range;
            }
        }
        int slop = u->t_turn ? 8 : 14;                 // a turn or a zoom should start soon, not jump
        if (u->t_ns >= 3 && abs(x - u->t_x0) + abs(y - u->t_y0) > slop) u->t_moved = 1;
        if (u->t_moved && u->t_turn) {
            float dx = (float)(x - view_cx(u)), dy = (float)(SCY - y);
            if (u->t_turn == 1) {
                float da = atan2f(dx, dy) / D2R - u->t_a0;   // the finger went round this far
                u->cfg.up = (int16_t)((int)lroundf(wrap360(u->t_up0 - da)) % 360);
            } else {
                float r = sqrtf(dx * dx + dy * dy);
                if (r < 8.0f) r = 8.0f;
                u->cfg.range = clampf(u->t_range0 * u->t_r0 / r, RANGE_MIN, RANGE_MAX);
            }
        } else if (u->t_moved && (u->view == VIEW_LIST || u->view == VIEW_SETUP || u->view == VIEW_WIFI)) {
            const int list = u->view == VIEW_LIST, wifi = u->view == VIEW_WIFI;
            int rows = list ? u->n_order : wifi ? wifi_rows(u) : setup_rows(u);
            int rh = list ? LIST_ROW : wifi ? NET_H : ROW_H;
            int y1 = list ? LIST_Y1 : BAR_Y - 3;
            int maxs = rows * rh - (y1 - LIST_Y0);
            int s = u->t_scroll0 - (y - u->t_y0);
            if (s > maxs) s = maxs;
            if (s < 0) s = 0;
            if (list) u->list_scroll = s;
            else if (wifi) u->wifi_scroll = s;
            else u->setup_scroll = s;
        } else if (!u->t_moved && u->t_rpt && (int32_t)(now - u->t_rep) >= 0) {
            act(u, u->t_hit, 0, u->t_x0, u->t_y0, cmd);
            u->t_reps++;
            u->t_rep = now + (now - u->t_since > 2500u ? 50u : 110u);
        }
        if (!u->t_moved && !u->t_long && !u->t_rpt && now - u->t_since >= 1200u) {
            u->t_long = 1;
            act(u, u->t_hit, 1, u->t_x0, u->t_y0, cmd);
        }
        if (!u->t_moved && u->t_long == 1 && u->t_hit == HIT_NONE && now - u->t_since >= 4000u) {
            u->t_long = 2;
            ui_start_calibration(u);                   // hold 4 s on nothing: align the touch again
        }
    } else if (u->t_down) {
        u->t_down = 0;
        int dx = u->t_x - u->t_x0, dy = u->t_y - u->t_y0;
        if (!u->t_moved && !u->t_long && !u->t_reps) {
            int n = u->t_ns > 2 ? u->t_ns - 1 : u->t_ns;
            int tx = median16(u->t_sx, n), ty = median16(u->t_sy, n);
            act(u, hit_at(u, tx, ty), 0, tx, ty, cmd);
        } else if (u->t_moved && u->t_turn == 1) {
            if (u->cfg.up <= 4 || u->cfg.up >= 356) { u->cfg.up = 0; toast(u, "NORTH UP"); }   // close is north
            cmd->cfg = 1;
        } else if (u->t_moved && u->t_turn == 2) {
            u->cfg.range = range_snap(u->cfg.range);
            say_range(u);
            cmd->cfg = cmd->refetch = 1;
        } else if (u->t_moved && u->view == VIEW_MAIN && abs(dx) > 70 && abs(dx) > 2 * abs(dy)) {
            u->view = dx < 0 ? VIEW_LIST : VIEW_SETUP;  // a swipe on the panel
        }
        u->t_turn = 0;
        u->t_hit = HIT_NONE;
    }
}

// BOOT: short = next range, long = back to the scope. on the alignment page: skip it
void ui_button(ui_t *u, int ev, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { u->view = VIEW_MAIN; toast(u, "TOUCH SKIPPED"); return; }
#if !RADAR_HAS_TOUCH
    /* no touch (the RPi LCD): a press steps through the planes on the scope, nearest first, and
     * past the last one lets go. held: the next range */
    if (ev == 1) {
        int at = -1, next = -1;
        for (int k = 0; k < u->n_order; k++) if (u->snap.p[u->order[k]].icao == u->sel) at = k;
        for (int k = at + 1; k < u->n_order && next < 0; k++) if (u->spot[u->order[k]].vis == 1) next = u->order[k];
        select_plane(u, next);
    } else {
        next_range(u, cmd);
    }
    return;
#endif
    if (ev == 2) {
        u->view = VIEW_MAIN;
        u->sel = 0;
        memset(u->wifi_pass, 0, sizeof u->wifi_pass);   // left the keyboard: what was typed goes
        return;
    }
    next_range(u, cmd);
}
```

# Air Quality core/ui.c: 9 crosses, a quadratic map, a sanity check

For a resistive panel that bends: worst cross 8 px measured on hardware.

```c
/* --- touch alignment: 9 crosses, median of each press, least squares on a quadratic map --- */

#define NT 6
static void terms(float rx, float ry, float *f)
{
    float u = rx / 4096.0f, v = ry / 4096.0f;
    f[0] = 1.0f; f[1] = u; f[2] = v; f[3] = u * v; f[4] = u * u; f[5] = v * v;
}

static int solve(float m[NT][NT + 1], float *x)                 // Gauss with partial pivoting
{
    for (int c = 0; c < NT; c++) {
        int p = c;
        for (int r = c + 1; r < NT; r++) if (fabsf(m[r][c]) > fabsf(m[p][c])) p = r;
        if (fabsf(m[p][c]) < 1e-9f) return 0;
        if (p != c) for (int j = 0; j <= NT; j++) { float t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        for (int r = 0; r < NT; r++) {
            if (r == c) continue;
            float f = m[r][c] / m[c][c];
            for (int j = c; j <= NT; j++) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < NT; i++) x[i] = m[i][NT] / m[i][i];
    return 1;
}

static void map_f(const touch_cal_t *c, float rx, float ry, float *x, float *y)
{
    float f[NT];
    terms(rx, ry, f);
    *x = *y = 0.0f;
    for (int i = 0; i < NT; i++) { *x += c->ax[i] * f[i]; *y += c->ay[i] * f[i]; }
}

void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y)
{
    float fx, fy;
    map_f(c, (float)rx, (float)ry, &fx, &fy);
    *x = (int)lroundf(fx);
    *y = (int)lroundf(fy);
}

int touch_cal_sane(const touch_cal_t *c)
{
    if (c->magic != TOUCH_CAL_MAGIC) return 0;
    float xs[4], ys[4];
    const float r[4][2] = { { 400, 400 }, { 3700, 400 }, { 400, 3700 }, { 3700, 3700 } };
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i < 4; i++) {
        map_f(c, r[i][0], r[i][1], &xs[i], &ys[i]);
        if (xs[i] != xs[i] || ys[i] != ys[i]) return 0;          // NaN
        if (xs[i] < -250 || xs[i] > GFX_W + 250 || ys[i] < -250 || ys[i] > GFX_H + 250) return 0;
        mnx = fminf(mnx, xs[i]); mxx = fmaxf(mxx, xs[i]); mny = fminf(mny, ys[i]); mxy = fmaxf(mxy, ys[i]);
    }
    return mxx - mnx > GFX_W * 0.5f && mxy - mny > GFX_H * 0.5f;
}
```

```c
static void cal_touch(ui_t *u, int down, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->cal_wait) { if (!down) u->cal_wait = 0; return; }
    if (down) {
        if (!u->t_down) { u->t_down = 1; u->t_since = now; u->cal_n = 0; }
        if (now - u->t_since > 120u && u->cal_n < 48) {
            u->cal_sx[u->cal_n] = (int16_t)rx;
            u->cal_sy[u->cal_n] = (int16_t)ry;
            u->cal_n++;
        }
        return;
    }
    if (!u->t_down) return;
    u->t_down = 0;
    if (u->cal_n < 2 || now - u->t_since < 300u) return;     // too short, just a brush
    int n = u->cal_n > 4 ? u->cal_n - 2 : u->cal_n;
    u->cal_raw[u->cal_step][0] = (float)median16(u->cal_sx, n);
    u->cal_raw[u->cal_step][1] = (float)median16(u->cal_sy, n);
    if (++u->cal_step < CAL_POINTS) return;

    float mx[NT][NT + 1] = { { 0 } }, my[NT][NT + 1] = { { 0 } };
    touch_cal_t fit = { .magic = TOUCH_CAL_MAGIC };
    for (int i = 0; i < CAL_POINTS; i++) {
        float f[NT];
        terms(u->cal_raw[i][0], u->cal_raw[i][1], f);
        for (int r = 0; r < NT; r++) {
            for (int c = 0; c < NT; c++) { mx[r][c] += f[r] * f[c]; my[r][c] += f[r] * f[c]; }
            mx[r][NT] += f[r] * (float)CAL_PT[i][0];
            my[r][NT] += f[r] * (float)CAL_PT[i][1];
        }
    }
    int ok = solve(mx, fit.ax) && solve(my, fit.ay);
    float worst = 0;
    if (ok)
        for (int i = 0; i < CAL_POINTS; i++) {
            float x, y;
            map_f(&fit, u->cal_raw[i][0], u->cal_raw[i][1], &x, &y);
            float ex = x - (float)CAL_PT[i][0], ey = y - (float)CAL_PT[i][1];
            float e = sqrtf(ex * ex + ey * ey);
            if (e > worst) worst = e;
        }
    u->cal_err = worst;
    if (!ok || worst > 14.0f || !touch_cal_sane(&fit)) {      // 9 points and 6 terms: a good press fits within a few px
        toast(u, "TRY AGAIN");
        u->cal_step = 0;
        return;
    }
    u->cal = fit;
    cmd->save_cal = 1;
    u->view = u->prev_view = VIEW_HOME;
    toast(u, "TOUCH ALIGNED");
}
```

# Thermo Relay core/ui.c: the first touch on a dark screen only wakes it

```c
int ui_backlight(const ui_t *u)
{
    return u->dimmed ? 6 : 100;
}

/* --- per frame --- */

void ui_frame(ui_t *u, uint32_t now)
{
    u->now = now;
    // dims after a while without a touch, back on the main page. not while something is wrong
    uint32_t ds = DIM_S[u->cfg.dim < 4 ? u->cfg.dim : 1];
    int keep_lit = u->snap.fault || u->view == VIEW_CAL || u->first_run || u->t_down;
    if (keep_lit) {
        u->dimmed = 0;
    } else if (ds && !u->dimmed && (int32_t)(now - u->last_input) > (int32_t)(ds * 1000u)) {
        u->dimmed = 1;
        u->view = VIEW_MAIN;
        u->cursor = -1;
    }
    if (u->cursor >= 0 && !u->t_down && now - u->cursor_t > 10000u) u->cursor = -1;
    if (u->view == VIEW_MAIN) main_frame(u);
    if (u->view == VIEW_GRAPH) plot_prepare(u, &GRAPH_PLOT[u->range]);
}
```

```c
void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { cal_touch(u, down, rx, ry, now, cmd); return; }
    if (down) u->last_input = now;
    if (u->dimmed || u->swallow) {                   // a dimmed screen: the first touch only wakes it
        if (down) { u->dimmed = 0; u->swallow = 1; }
        else u->swallow = 0;
        return;
    }
```

# Flight Radar core/ui_wifi.c: the network list and the keyboard

```c
/* the WiFi page: the networks the board hears, strongest first. a tap on one asks for its
 * password on the keyboard page, an open one is joined at once. the password goes to the
 * firmware with ui_cmd_t.wifi and the firmware wipes the copy here. the site can still send
 * WiFi over the USB line as before */
#include "ui_int.h"

/* --- the networks --- */

static int net_y(const ui_t *u, int k) { return LIST_Y0 + k * NET_H - u->wifi_scroll; }

int wifi_rows(const ui_t *u) { return u->net.n_aps; }

void wifi_open(ui_t *u, ui_cmd_t *cmd)
{
    u->view = VIEW_WIFI;
    u->wifi_scroll = 0;
    u->net.scan = SCAN_RUNNING;                        // till the firmware says how it went
    cmd->scan = 1;
}

int wifi_hit(const ui_t *u, int x, int y)
{
    int b = bar_hit(x, y, 2);
    if (b >= 0) return HIT_BAR + b;
    if (y < LIST_Y0 || y > BAR_Y - 3) return HIT_NONE;
    int k = (y - LIST_Y0 + u->wifi_scroll) / NET_H;
    return k >= 0 && k < u->net.n_aps ? HIT_NET + k : HIT_NONE;
}

// four bars, as many lit as the signal is strong
static void signal_bars(canvas_t *cv, int x, int y, int rssi, uint32_t c)
{
    int lit = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 3;
        gfx_fill(cv, x + i * 5, y + 13 - h, 3, h, i < lit ? c : C_QUIET, i < lit ? 240 : 120);
    }
}

// a small lock: the network wants a password
static void lock_mark(canvas_t *cv, int x, int y, uint32_t c)
{
    gfx_arc(cv, x + 4.5f, y + 5.0f, 2.8f, -90.0f, 90.0f, 1.3f, c, 230);
    gfx_fill(cv, x, y + 5, 10, 7, c, 230);
}

void wifi_draw(ui_t *u, canvas_t *cv)
{
    int y1 = BAR_Y - 3;
    int ya = cv->y0 > LIST_Y0 ? cv->y0 : LIST_Y0, yb = cv->y0 + cv->h - 1 < y1 ? cv->y0 + cv->h - 1 : y1;
    if (ya <= yb) {
        canvas_t lc = { cv->px + (ya - cv->y0) * GFX_W, ya, yb - ya + 1, cv->helper };
        for (int k = 0; k < u->net.n_aps; k++) {
            int y = net_y(u, k);
            if (!gfx_touches(&lc, y, y + NET_H)) continue;
            const ap_t *a = &u->net.aps[k];
            int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_NET + k;
            int now = u->net.configured && !strcmp(a->ssid, u->net.ssid);   // the one that is set
            if (pressed) gfx_fill(&lc, PG_L + 4, y, PG_R - PG_L - 8, NET_H - 2, C_PRIM, 70);
            gfx_hline(&lc, PG_L + 6, PG_R - 7, y + NET_H - 1, C_QUIET, 70);
            signal_bars(&lc, 12, y + (NET_H - 14) / 2, a->rssi, now ? C_FOCUS : C_PRIM);
            const int tx = 40, ty = y + (NET_H - FONT_MEDIUM.line_h) / 2;
            font_text_clip(&lc, &FONT_MEDIUM, tx, ty, a->ssid, now ? C_FOCUS : C_PRIM, 255, GFX_W - tx - 90);
            const char *tag = now ? (u->net.wifi ? "JOINED" : "SET") : a->open ? "OPEN" : NULL;
            int rx = GFX_W - 14;
            if (!a->open) {
                lock_mark(&lc, rx - 10, y + (NET_H - 12) / 2, C_DIM);
                rx -= 18;
            }
            if (tag) micro_text(&lc, rx - micro_width(tag, 1), y + (NET_H - 5) / 2, tag, now ? C_FOCUS : C_DIM, 235, 1);
        }
        if (!u->net.n_aps) {
            const char *s = u->net.scan == SCAN_RUNNING ? "LOOKING FOR NETWORKS" : u->net.scan == SCAN_FAILED ?
                            "COULD NOT LOOK: TRY AGAIN" : "NO NETWORK HEARD";
            micro_text(&lc, GFX_W / 2 - micro_width(s, 2) / 2, LIST_Y0 + 40, s, C_DIM, 235, 2);
        }
    }
    const char *right = u->net.scan == SCAN_RUNNING ? "LOOKING..." : "TAP A NETWORK";
    page_header(cv, "WIFI", right, C_FOCUS);
    static const char *const L[2] = { "LOOK AGAIN", "BACK" };
    bar(u, cv, L, 2, -1);
}

void wifi_act(ui_t *u, int hit, ui_cmd_t *cmd)
{
    if (hit == HIT_BAR + 0) { wifi_open(u, cmd); return; }
    if (hit == HIT_BAR + 1) { u->view = VIEW_MAIN; return; }
    int k = hit - HIT_NET;
    if (k < 0 || k >= u->net.n_aps) return;
    const ap_t *a = &u->net.aps[k];
    snprintf(u->wifi_ssid, sizeof u->wifi_ssid, "%s", a->ssid);
    memset(u->wifi_pass, 0, sizeof u->wifi_pass);
    if (a->open) {                                     // nothing to type: join it
        cmd->wifi = 1;
        u->view = VIEW_MAIN;
        toast(u, "JOINING THE WIFI");
        return;
    }
    u->view = VIEW_KEYS;
    u->kb_mode = KB_PASS;
    u->kb_page = 0;
    u->kb_show = 0;
    u->kb_typed = 0;
}

/* the same keyboard for a new home: a town, a postcode, or the point as "lat lon". the firmware
 * asks OpenStreetMap's Nominatim where a name is; a point needs no WiFi */
void home_open(ui_t *u)
{
    memset(u->home_text, 0, sizeof u->home_text);
    u->view = VIEW_KEYS;
    u->kb_mode = KB_HOME;
    u->kb_page = 0;
    u->kb_typed = 0;
}

/* and for the watch list: what is on it now, to change. letters come out as capitals, the way
 * callsigns and registrations are written */
void watch_open(ui_t *u)
{
    watch_text(&u->watch, u->watch_text, sizeof u->watch_text);
    u->view = VIEW_KEYS;
    u->kb_mode = KB_WATCH;
    u->kb_page = 0;
    u->kb_typed = 0;
}

/* --- the keyboard: ten keys a row, four rows, three pages. a run of the same special key is one
 * wide key. every sign a WiFi password may have is on one of them --- */

enum { K_SHIFT = 1, K_RUB, K_SIGNS, K_LETTERS };

static const char KEYS[3][4][11] = {
    { "1234567890", "qwertyuiop", "asdfghjkl\x02", "\x01zxcvbnm.\x03" },
    { "1234567890", "QWERTYUIOP", "ASDFGHJKL\x02", "\x01ZXCVBNM.\x03" },
    { "!@#$%^&*()", "-_=+[]{}\\|", ":;'\",./?`\x02", "\x04<>~      " },
};

#define FIELD_Y (HEAD_H + (GFX_H >= 320 ? 18 : 15))
#define FIELD_H (KB_Y0 - 5 - FIELD_Y)

// what the keys type into: the password, the home or the watch list
static char *typed(ui_t *u, int *cap)
{
    if (u->kb_mode == KB_HOME) { *cap = (int)sizeof u->home_text; return u->home_text; }
    if (u->kb_mode == KB_WATCH) { *cap = (int)sizeof u->watch_text; return u->watch_text; }
    *cap = (int)sizeof u->wifi_pass;
    return u->wifi_pass;
}

static int key_code(const ui_t *u, int cell)
{
    return (unsigned char)KEYS[u->kb_page < 3 ? u->kb_page : 0][cell / 10][cell % 10];
}

// the run of equal special keys cell is in: one wide key
static void key_span(const ui_t *u, int cell, int *a, int *b)
{
    int c = key_code(u, cell), r = cell / 10 * 10;
    *a = *b = cell;
    if (c > ' ') return;                               // letters and signs are one cell each
    while (*a > r && key_code(u, *a - 1) == c) (*a)--;
    while (*b < r + 9 && key_code(u, *b + 1) == c) (*b)++;
}

int keys_hit(const ui_t *u, int x, int y)
{
    int b = bar_hit(x, y, u->kb_mode == KB_PASS ? 3 : 2);
    if (b >= 0) return HIT_BAR + b;
    if (y < KB_Y0 || y >= KB_Y0 + 4 * KB_RH) return HIT_NONE;
    int col = x / KB_CW, row = (y - KB_Y0) / KB_RH;
    if (col < 0 || col > 9 || row < 0 || row > 3) return HIT_NONE;
    return HIT_KEY + row * 10 + col;
}

int keys_repeats(const ui_t *u, int hit)
{
    return hit >= HIT_KEY && hit < HIT_KEY + 40 && key_code(u, hit - HIT_KEY) == K_RUB;
}

// what a special key says on it
static const char *key_name(const ui_t *u, int c)
{
    switch (c) {
    case K_SHIFT:   return u->kb_page == 1 ? "abc" : "ABC";
    case K_RUB:     return "DEL";
    case K_SIGNS:   return "#+=";
    case K_LETTERS: return "abc";
    case ' ':       return "SPACE";
    default:        return NULL;
    }
}

// the password field: dots, the last letter a moment in clear; or all of it in clear. the home
// and the watch list are always in clear
static void field(ui_t *u, canvas_t *cv)
{
    const int x = 6, w = GFX_W - 12, y = FIELD_Y, h = FIELD_H;
    if (!gfx_touches(cv, y - 12, y + h)) return;
    const char *above = u->kb_mode == KB_HOME ? "a town, a postcode, or lat lon: 48.9 18.1" :
                        u->kb_mode == KB_WATCH ? "callsigns, registrations, hex: EZY12AB, G-EUPT" : u->wifi_ssid;
    font_text_clip(cv, &FONT_SMALL, x + 2, y - 12, above, C_DIM, 235, w - 4);
    chamfer_box(cv, x, y, w, h, C_BG0, 200, C_PRIM, 220);
    int cap;
    const char *text = typed(u, &cap);
    const int n = (int)strlen(text), room = w - 20, ty = y + (h - FONT_MEDIUM.line_h) / 2;
    int px = x + 8;
    if (u->kb_show || u->kb_mode != KB_PASS) {
        const char *s = text;
        while (*s && font_width(&FONT_MEDIUM, s) > room) s++;   // the end of it, where the typing is
        px += font_text(cv, &FONT_MEDIUM, px, ty, s, C_FOCUS, 255);
    } else {
        const int dw = 9, fresh = n && u->now - u->kb_typed < 1200u;
        int first = 0;
        while ((n - first) * dw > room) first++;
        for (int i = first; i < n; i++) {
            if (i == n - 1 && fresh) {                 // the letter just typed, before it hides
                char one[2] = { u->wifi_pass[i], 0 };
                px += font_text(cv, &FONT_MEDIUM, px, ty, one, C_FOCUS, 255);
            } else {
                gfx_disc(cv, px + 3.5f, y + h / 2.0f, 2.6f, C_PRIM, 255);
                px += dw;
            }
        }
    }
    if ((u->now / 500u) & 1) gfx_vline(cv, px + 1, y + 5, y + h - 6, C_FOCUS, 230);   // where the next letter goes
    static const char *const HINT[3] = { "PASSWORD", "HOME", "NONE: NOTHING WATCHED" };
    if (!n) micro_text(cv, x + w - 8 - micro_width(HINT[u->kb_mode], 1), y + (h - 5) / 2, HINT[u->kb_mode], C_QUIET, 220, 1);
}

void keys_draw(ui_t *u, canvas_t *cv)
{
    static const char *const HEAD[3] = { "WIFI PASSWORD", "HOME", "WATCH" };
    page_header(cv, HEAD[u->kb_mode], NULL, C_FOCUS);
    field(u, cv);
    for (int row = 0; row < 4; row++) {
        int y = KB_Y0 + row * KB_RH;
        if (!gfx_touches(cv, y, y + KB_RH)) continue;
        for (int col = 0; col < 10; col++) {
            int a, b, cell = row * 10 + col;
            key_span(u, cell, &a, &b);
            if (a != cell) continue;                   // drawn with the first cell of its run
            int c = key_code(u, cell), x = a % 10 * KB_CW + 2, w = (b - a + 1) * KB_CW - 4;
            int pressed = u->t_down && !u->t_moved && u->t_hit >= HIT_KEY + a && u->t_hit <= HIT_KEY + b;
            int lit = c == K_SHIFT && u->kb_page == 1;
            const char *name = key_name(u, c);
            if (name) {
                button(cv, x, y + 2, w, KB_RH - 4, NULL, pressed ? 2 : lit);
                micro_text(cv, x + (w - micro_width(name, 1)) / 2, y + KB_RH / 2 - 2, name, pressed || lit ? C_FOCUS : C_PRIM, 240, 1);
            } else {
                char one[2] = { (char)c, 0 };
                button(cv, x, y + 2, w, KB_RH - 4, NULL, pressed ? 2 : 0);
                font_text(cv, &FONT_MEDIUM, x + (w - font_width(&FONT_MEDIUM, one)) / 2, y + (KB_RH - FONT_MEDIUM.line_h) / 2,
                          one, pressed ? C_FOCUS : C_PRIM, 255);
            }
        }
    }
    if (u->kb_mode == KB_HOME) {
        static const char *const H[2] = { "CANCEL", "FIND" };
        bar(u, cv, H, 2, u->home_text[0] ? 1 : -1);
        return;
    }
    if (u->kb_mode == KB_WATCH) {
        static const char *const W[2] = { "CANCEL", "SAVE" };
        bar(u, cv, W, 2, 1);
        return;
    }
    const char *L[3] = { u->kb_show ? "HIDE" : "SHOW", "CANCEL", "JOIN" };
    bar(u, cv, L, 3, u->wifi_pass[0] ? 2 : -1);
}

// the two keys under the keyboard for the home and the watch list. 1: it was one of them
static int text_bar(ui_t *u, int hit, ui_cmd_t *cmd)
{
    if (hit == HIT_BAR + 0) { u->view = VIEW_SETUP; return 1; }
    if (hit != HIT_BAR + 1) return 0;
    if (u->kb_mode == KB_WATCH) {                      // empty is fine: nothing watched
        cmd->watch = 1;
        u->view = VIEW_SETUP;
        toast(u, u->watch_text[0] ? "WATCH LIST SAVED" : "NOTHING WATCHED");
        return 1;
    }
    if (!u->home_text[0]) { toast(u, "TYPE A TOWN, OR LAT LON"); return 1; }
    cmd->home = 1;                                     // the firmware says what it found
    u->view = VIEW_SETUP;
    toast(u, "LOOKING FOR IT");
    return 1;
}

void keys_act(ui_t *u, int hit, ui_cmd_t *cmd)
{
    if (u->kb_mode != KB_PASS && text_bar(u, hit, cmd)) return;
    if (u->kb_mode == KB_PASS && hit == HIT_BAR + 0) { u->kb_show ^= 1; return; }
    if (hit == HIT_BAR + 1) {                          // back to the networks, the letters forgotten
        memset(u->wifi_pass, 0, sizeof u->wifi_pass);
        u->view = VIEW_WIFI;
        return;
    }
    if (hit == HIT_BAR + 2) {
        if (!u->wifi_pass[0]) { toast(u, "TYPE THE PASSWORD"); return; }
        cmd->wifi = 1;
        u->view = VIEW_MAIN;
        toast(u, "JOINING THE WIFI");
        return;
    }
    if (hit < HIT_KEY || hit >= HIT_KEY + 40) return;
    int cap;
    char *t = typed(u, &cap);
    int c = key_code(u, hit - HIT_KEY), n = (int)strlen(t);
    switch (c) {
    case K_SHIFT:   u->kb_page = u->kb_page == 1 ? 0 : 1; break;
    case K_SIGNS:   u->kb_page = 2; break;
    case K_LETTERS: u->kb_page = 0; break;
    case K_RUB:     if (n) t[n - 1] = 0; u->kb_typed = 0; break;
    default:
        if (n >= cap - 1) return;
        if (u->kb_mode == KB_WATCH && c >= 'a' && c <= 'z') c -= 32;   // EZY12AB, G-EUPT: capitals
        t[n] = (char)c;
        t[n + 1] = 0;
        u->kb_typed = u->now;
        if (u->kb_page == 1) u->kb_page = 0;           // a capital, then small letters again
        break;
    }
}
```

# Flight Radar core/ui_scope.c: hit areas of the main page

```c
int main_hit(const ui_t *u, int x, int y)
{
#if RADAR_ROUND
    int dx = x - SCX, dy = y - SCY;
    if (dx * dx + dy * dy > (SR + 8) * (SR + 8)) return HIT_NONE;
    if (menu_up(u) && y >= MENU_Y - 2 && y < MENU_Y + MENU_H + 2)
        for (int i = 0; i < MENU_N; i++) if (x >= menu_x(i) && x < menu_x(i) + MENU_W) return menu_hit(i);
    if (u->sel && in_box(x, y, SCX - CARD_W / 2, CARD_Y, CARD_W, CARD_H)) return HIT_PANEL;
    note_box_t nb;
    if (update_note_box(u, &nb) && in_box(x, y, nb.x - 6, nb.y - 6, nb.w + 12, nb.h + 12)) return HIT_NEWS;
    return HIT_SCOPE;
#else
    note_box_t nb;                                     // a finger is wide: the note answers round it too
    if (update_note_box(u, &nb) && in_box(x, y, nb.x - 6, nb.y - 6, nb.w + 12, nb.h + 12)) return HIT_NEWS;
    // the buttons over the map answer further out than they are drawn: a finger is wide, a
    // resistive panel is a few pixels off
#if UI_WIFI_PAGE
    if (demo_shown(u)) {
        const demo_box_t d = demo_box(u);
        if (in_box(x, y, d.bx - 8, d.by - 8, d.bw + 16, d.bh + 16)) return HIT_WIFI;
    }
#endif
    const int lx = look_x(u), ly = LOOK_Y + LOOK_H + 12;
    if (square_look(u)) {
        if (y < ly && x >= lx + LOOK_W + 2 && x < view_right(u)) return HIT_DOTS;
        if (y < ly && x >= lx - 10 && x < lx + LOOK_W + 2) return look_hit(u, x);   // HIT_LOOK_BACK left of its middle
        if (!panel_shown(u) || x < PX - 2) return HIT_SCOPE;   // the square map: all of it but the panel
    } else if (y < ly && x >= lx - 12 && x < PX - 2) {
        return look_hit(u, x);
    }
    if (x < PX - 2) {
        int dx = x - SCX, dy = y - SCY;
        return dx * dx + dy * dy <= (SR + 6) * (SR + 6) ? HIT_SCOPE : HIT_NONE;
    }
    if (y >= BTN_Y - 2) {
        int b = (x - btn_x(0)) / (BTN_W + 3);
        return b >= 0 && b < 3 ? HIT_BTN + b : HIT_NONE;
    }
    if (!u->sel && y >= NEAR_Y0 && y < NEAR_Y0 + NEAR_N * NEAR_H) {
        int k = (y - NEAR_Y0) / NEAR_H;
        return k < u->n_order ? HIT_ROW + k : HIT_NONE;
    }
    return HIT_PANEL;
#endif
}
```

# Flight Radar core/ui_pages.c: the alignment page

```c
/* --- touch alignment --- */

void cal_draw(ui_t *u, canvas_t *cv)
{
    char buf[32];
    int s = u->cal_step < CAL_POINTS ? u->cal_step : CAL_POINTS - 1;
    float x = (float)CAL_PT[s][0], y = (float)CAL_PT[s][1];
    float f = (float)(u->now % 1000u) / 1000.0f;
    gfx_ring(cv, x, y, 6.0f + 14.0f * f, 1.2f, C_FOCUS, (int)(200 * (1.0f - f)));
    gfx_line(cv, x - 14, y, x + 14, y, 1.2f, C_FOCUS, 240);
    gfx_line(cv, x, y - 14, x, y + 14, 1.2f, C_FOCUS, 240);
    gfx_ring(cv, x, y, 6.0f, 1.2f, C_PRIM, 240);
    if (u->t_down) gfx_disc(cv, x, y, 5.0f, C_FOCUS, 200);
    int ty = CAL_PT[s][1] <= GFX_H / 2 ? GFX_H / 2 + 26 : GFX_H / 2 - 66;
    const char *t1 = "TOUCH ALIGNMENT";
    micro_text(cv, GFX_W / 2 - micro_width(t1, 2) / 2, ty, t1, C_FOCUS, 250, 2);
    const char *t2 = "PRESS AND HOLD THE CENTRE OF THE CROSS";
    micro_text(cv, GFX_W / 2 - micro_width(t2, 1) / 2, ty + 20, t2, C_DIM, 235, 1);
    snprintf(buf, sizeof buf, "POINT %d OF %d", s + 1, CAL_POINTS);
    micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, ty + 34, buf, C_PRIM, 240, 1);
    if (ui_screen_name) {
        /* a board sold with different screens: this page is the first thing after flashing,
         * the place to say how to pick the other ones */
        const char *t4 = "SCREEN WRONG? PRESS BOOT";
        micro_text(cv, GFX_W / 2 - micro_width(t4, 1) / 2, ty + 48, t4, C_PRIM, 240, 1);
        char t5[48];
        snprintf(t5, sizeof t5, "NOW %s   HOLD BOOT: SKIP", ui_screen_name);
        micro_text(cv, GFX_W / 2 - micro_width(t5, 1) / 2, ty + 57, t5, C_QUIET, 230, 1);
    } else {
        const char *t4 = "BOOT BUTTON: SKIP";
        micro_text(cv, GFX_W / 2 - micro_width(t4, 1) / 2, ty + 48, t4, C_QUIET, 230, 1);
    }
}
```

