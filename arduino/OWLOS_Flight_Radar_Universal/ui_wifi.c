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
    canvas_t lc;
    if (gfx_sub(cv, LIST_Y0, BAR_Y - 3, &lc)) {
        for (int k = 0; k < u->net.n_aps; k++) {
            int y = net_y(u, k);
            if (!gfx_touches(&lc, y, y + NET_H)) continue;
            const ap_t *a = &u->net.aps[k];
            int pressed = u->t_down && !u->t_moved && u->t_hit == HIT_NET + k;
            int now = u->net.configured && !strcmp(a->ssid, u->net.ssid);   // the one that is set
            if (pressed) gfx_fill(&lc, PG_L + 4, y, PG_R - PG_L - 8, NET_H - 2, C_PRIM, 70);
            gfx_hline(&lc, PG_L + 6, PG_R - 7, y + NET_H - 1, C_QUIET, 70);
            signal_bars(&lc, 12, y + (NET_H - 14) / 2, a->rssi, now ? C_FOCUS : C_PRIM);
            const int tx = 40, ty = y + (NET_H - FONT_LINE_H(&FONT_MEDIUM)) / 2;
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
    const int n = (int)strlen(text), room = w - 20, ty = y + (h - FONT_LINE_H(&FONT_MEDIUM)) / 2;
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
                font_text(cv, &FONT_MEDIUM, x + (w - font_width(&FONT_MEDIUM, one)) / 2, y + (KB_RH - FONT_LINE_H(&FONT_MEDIUM)) / 2,
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
