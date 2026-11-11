/* ABOUT: which version this is, whether the site has a newer one, the way to the site (its address
 * and a QR code of it), the board, the FR24 key. opened from SET's DATA tab, and from the note on
 * the main screen when the site has a newer version (ui_scope.c draws that one) */
#include "ui_int.h"
#include "qr_site.h"

const char *ui_version;

// one number of a version and the dot after it: "0.10" gives 0, then 10
static long part(const char **s)
{
    long v = 0;
    while (**s >= '0' && **s <= '9') v = v * 10 + (*(*s)++ - '0');   // the next digit
    if (**s == '.') (*s)++;
    return v;
}

int ui_version_cmp(const char *a, const char *b)
{
    for (int k = 0; k < 8 && (*a || *b); k++) {        // no version has eight numbers: a stray letter ends it
        long x = part(&a), y = part(&b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

int ui_update(const ui_t *u)
{
    return ui_version && u->newest[0] && ui_version_cmp(u->newest, ui_version) > 0;
}

void update_text(const ui_t *u, char *buf, int sz, int short_form)
{
    snprintf(buf, (size_t)sz, short_form ? "NEW %s" : "NEW VERSION %s", u->newest);
}

void about_open(ui_t *u)
{
    u->about_back = u->view == VIEW_ABOUT ? VIEW_MAIN : u->view;
    u->view = VIEW_ABOUT;
}

/* the code: dark modules on a light plate with a light edge round it, as printed ones are. a phone
 * reads a light code on a dark screen less surely. modules as big as the screen lets them be */
#if RADAR_ROUND
#define QR_PX 3                    /* pixels a module */
#elif GFX_H >= 320
#define QR_PX 5
#else
#define QR_PX 4
#endif
#define QR_EDGE 2                  /* modules of light round it */
#define QR_W    ((QR_SITE_N + 2 * QR_EDGE) * QR_PX)
#define QR_LIGHT RGB(250, 250, 247)
#define QR_DARK  RGB(10, 12, 20)

static void qr(canvas_t *cv, int x, int y)
{
    if (!gfx_touches(cv, y, y + QR_W)) return;
    gfx_fill(cv, x, y, QR_W, QR_W, QR_LIGHT, 255);
    const int x0 = x + QR_EDGE * QR_PX, y0 = y + QR_EDGE * QR_PX;
    for (int r = 0; r < QR_SITE_N; r++) {
        const int py = y0 + r * QR_PX;                 // the row's top
        if (!gfx_touches(cv, py, py + QR_PX)) continue;
        const uint8_t *row = QR_SITE + r * QR_SITE_ROW;
        for (int c = 0; c < QR_SITE_N;) {              // a run of dark modules is one fill
            if (!(row[c >> 3] & (0x80 >> (c & 7)))) { c++; continue; }
            int e = c;
            while (e < QR_SITE_N && (row[e >> 3] & (0x80 >> (e & 7)))) e++;
            gfx_fill(cv, x0 + c * QR_PX, py, (e - c) * QR_PX, QR_PX, QR_DARK, 255);   // modules c..e-1
            c = e;
        }
    }
}

// what the site said, in a word or two, and in which colour
static uint32_t site_said(const ui_t *u, char *b, int sz)
{
    if (!u->newest[0]) {
        snprintf(b, (size_t)sz, u->net.wifi ? "the site not asked yet" : "no WiFi: the site not asked");
        return C_DIM;
    }
    int c = ui_version && ui_version[0] ? ui_version_cmp(u->newest, ui_version) : 1;
    if (c > 0) snprintf(b, (size_t)sz, "%s is on the site", u->newest);
    else if (c == 0) snprintf(b, (size_t)sz, "the newest one");
    else snprintf(b, (size_t)sz, "newer than the site's %s", u->newest);
    return c > 0 ? C_FOCUS : C_DIM;
}

// the address without https:// and the last slash: what a person would type
static void site_name(char *b, int sz)
{
    const char *s = strstr(SITE_URL, "://");
    snprintf(b, (size_t)sz, "%s", s ? s + 3 : SITE_URL);
    int n = (int)strlen(b);
    if (n && b[n - 1] == '/') b[n - 1] = 0;
}

void about_draw(ui_t *u, canvas_t *cv)
{
    char said[48], ver[24], site[64];
    const uint32_t said_col = site_said(u, said, sizeof said);
    snprintf(ver, sizeof ver, "%s", ui_version && ui_version[0] ? ui_version : "?");
    site_name(site, sizeof site);
#if RADAR_ROUND
    // the glass: the code in the middle, the version over it, what the site said under it
    char head[32];
    snprintf(head, sizeof head, "VERSION %s", ver);
    qr(cv, (GFX_W - QR_W) / 2, HEAD_H + 4);
    int y = HEAD_H + 4 + QR_W + 4;
    if (gfx_touches(cv, y, y + 26)) {
        int w = font_width(&FONT_SMALL, said);
        font_text(cv, &FONT_SMALL, GFX_W / 2 - w / 2, y, said, said_col, 255);
        const char *scan = ui_update(u) ? "SCAN: THE SITE FLASHES IT" : "SCAN: THE SITE";
        micro_text(cv, GFX_W / 2 - micro_width(scan, 1) / 2, y + 15, scan, C_DIM, 235, 1);
    }
    page_header(cv, "ABOUT", head, C_FOCUS);
#else
    const int qx = GFX_W - 8 - QR_W, qy = LIST_Y0 + 4, x = 10;
    qr(cv, qx, qy);
    int y = LIST_Y0 + 6;
#define AY(small, tall) (GFX_H >= 320 ? (tall) : (small))   /* a line's place: the taller screen spaces them out */
    if (gfx_touches(cv, y, qy + QR_W)) {
        char line[48];
        micro_text(cv, x, y, "OWLOS FLIGHT RADAR", C_PRIM, 240, 1);
        micro_text(cv, x, y + AY(12, 14), "VERSION", C_QUIET, 230, 1);
        font_text(cv, &FONT_LARGE, x - 1, y + AY(20, 24), ver, C_FOCUS, 255);
        const int w = qx - x - 6;                      // the pixel font has no " and no &: these in the smooth one
        font_text_clip(cv, &FONT_SMALL, x, y + AY(44, 54), said, said_col, 250, w);
        font_text_clip(cv, &FONT_SMALL, x, y + AY(62, 78), RADAR_BOARD, C_DIM, 235, w);
        snprintf(line, sizeof line, "FR24 key: %s", u->net.has_key ? "stored" : "none");
        font_text_clip(cv, &FONT_SMALL, x, y + AY(76, 94), line, C_DIM, 235, w);
        if (ui_update(u)) {                            // how: the same page that flashed it, settings kept
            font_text_clip(cv, &FONT_SMALL, x, y + AY(96, 120), "to update: the site,", C_FOCUS, 250, w);
            font_text_clip(cv, &FONT_SMALL, x, y + AY(110, 136), "CONNECT & FLASH.", C_FOCUS, 250, w);
            font_text_clip(cv, &FONT_SMALL, x, y + AY(124, 152), "no erase: settings stay", C_FOCUS, 250, w);
        }
    }
#undef AY
    y = BAR_Y - 20;                                    // the address, for who would rather type it
    if (gfx_touches(cv, y, y + 14)) font_text_clip(cv, &FONT_SMALL, x, y, site, C_PRIM, 250, GFX_W - 2 * x);
    page_header(cv, "ABOUT", "SCAN IT: THE SITE", C_FOCUS);
#endif
    static const char *const L[1] = { "BACK" };
    bar(u, cv, L, 1, -1);
}

int about_hit(const ui_t *u, int x, int y)
{
    (void)u;
    return bar_hit(x, y, 1) == 0 ? HIT_BAR : HIT_NONE;
}

void about_act(ui_t *u, int hit)
{
    if (hit == HIT_BAR) u->view = u->about_back;
}
