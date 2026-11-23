# The PC tools, complete

From OWLOS-FLIGHT-RADAR `tools/`. Windows, Visual Studio 2022 C++ tools, .NET 8 SDK, ffmpeg. All were
run here. The tile fetch in `ground.inc` is only for projects with a map.

# tools/preview/BUILD.BAT

```bat
@echo off
rem build the PC preview of the flight radar's screen with MSVC and run it
rem   BUILD.BAT              the 2.8" CYD, screenshots go to ..\..\shots
rem   BUILD.BAT st7796       another screen (st7796 ili9486 gc9a01), shots go to ..\..\shots\<screen>
rem core\ sources are the same as in the firmware
setlocal
set DISP=%~1
if "%DISP%"=="" set DISP=ili9341
set NUM=
if /i "%DISP%"=="ili9341" set NUM=1
if /i "%DISP%"=="st7796"  set NUM=2
if /i "%DISP%"=="ili9486" set NUM=3
if /i "%DISP%"=="gc9a01"  set NUM=4
if "%NUM%"=="" (
  echo unknown screen %DISP%, pick one of: ili9341 st7796 ili9486 gc9a01
  exit /b 1
)
set OUT=..\..\shots
if not "%NUM%"=="1" set OUT=..\..\shots\%DISP%
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ tools not found: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist build\%DISP% mkdir build\%DISP%
rem the map's tiles from OpenStreetMap, fetched once by curl (ground.inc)
if not exist tiles mkdir tiles
rem the version, as the firmware takes it: the first "## " line of CHANGES.md
set VER=
for /f "tokens=2" %%v in ('findstr /b /c:"## " ..\..\CHANGES.md') do if not defined VER set VER=%%v
if "%VER%"=="" (
  echo no version in CHANGES.md: its first "## " line should be one, like ## 0.3
  exit /b 1
)
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"%VER%\" ^
   /Fobuild\%DISP%\ /Febuild\%DISP%\preview.exe ^
   preview.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ..\..\core\ui_scope.c ..\..\core\ui_pages.c ..\..\core\ui_wifi.c ..\..\core\ui_about.c ^
   ..\..\core\qr_site.c ..\..\core\map.c ..\..\core\png_decode.c ..\..\core\watch.c ..\..\core\light.c ^
   ..\..\core\font_small.c ..\..\core\font_medium.c ..\..\core\font_large.c ..\..\core\font_huge.c
if errorlevel 1 exit /b 1
if not exist %OUT% mkdir %OUT%
build\%DISP%\preview.exe %OUT%
```

# tools/preview/preview.c: the head of the script

`main()` after this is one long session of `fly`, `tap`, `shot`. Its shape is in pc-preview-tool.md.

```c
/*
 * preview - the flight radar's screen on PC, the same core/ code as the firmware. the demo sky
 * (core/demo.c) plays the free network: its JSON goes through the real parser. a script taps
 * like a finger and every page is saved as a PNG of what the panel shows (dithered RGB565).
 * usage: preview <out-dir>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/sky.h"
#include "../../core/demo.h"
#include "../../core/ui.h"
#include "../../core/ui_int.h"

#include "png.inc"
#include "ground.inc"

static sky_t   sky;
static demo_t  demo;
static ui_t    ui;
static uint32_t t_ms;
static const char *g_dir;
static char    json[32768];

#define HOME_LAT 48.8945f              // Trencin, the board's home till the site sends one
#define HOME_LON 18.0444f

// the sky moves on: an answer every 5 s, like the firmware asks
static void fly(int seconds)
{
    for (int s = 0; s < seconds; s++) {
        t_ms += 1000u;
        if (t_ms % 5000u == 0) {
            int n = demo_json(&demo, HOME_LAT, HOME_LON, range_km(&ui.cfg), t_ms, json, sizeof json);
            static sky_reader_t rd;                  // in pieces, like the network hands it over
            sky_read_begin(&rd, &sky, 0, t_ms);
            for (int at = 0; at < n; at += 37) sky_read(&rd, json + at, n - at < 37 ? n - at : 37);
            if (sky_read_end(&rd, &sky) < 0) printf("parse failed\n");
            sky_forget(&sky, t_ms);
        }
    }
}

#define STRIP 24
static uint32_t strip_px[GFX_W * STRIP];
static uint16_t frame565[GFX_W * GFX_H];

static void render(void)
{
    make_ground(&ui);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int y = 0; y < GFX_H; y += STRIP) {
        canvas_t cv = { strip_px, y, y + STRIP <= GFX_H ? STRIP : GFX_H - y, 0 };
        ui_strip(&ui, &cv);
        gfx_pack565(&cv, frame565 + y * GFX_W);
    }
}

static void shot(const char *name)
{
    render();
    const int zoom = 2, w = GFX_W * zoom, h = GFX_H * zoom;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t v = frame565[(y / zoom) * GFX_W + x / zoom];
            v = (uint16_t)((v >> 8) | (v << 8));
            uint8_t *o = rgb + ((size_t)y * w + x) * 3;
            o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((v & 31) * 255 / 31);
        }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", g_dir, name);
    if (write_png(path, rgb, w, h)) printf("wrote %s\n", path);
    free(rgb);
}

static void apply(const ui_cmd_t *c) { (void)c; }

static void tap(int x, int y)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int k = 0; k < 4; k++) ui_touch(&ui, 1, x, y, 0, 0, t_ms + k * 20u, &cmd);
    ui_touch(&ui, 0, x, y, 0, 0, t_ms + 100u, &cmd);
    apply(&cmd);
}

static void drag(int x0, int y0, int x1, int y1)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    for (int k = 0; k <= 10; k++) ui_touch(&ui, 1, x0 + (x1 - x0) * k / 10, y0 + (y1 - y0) * k / 10, 0, 0, t_ms + k * 20u, &cmd);
    ui_touch(&ui, 0, x1, y1, 0, 0, t_ms + 260u, &cmd);
}

// a finger put down, moved through points, lifted: like the touch task sends it, a sample every 20 ms
static void finger(const int (*pts)[2], int n, int lift)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int k = 0; k < n; k++) { ui_touch(&ui, 1, pts[k][0], pts[k][1], 0, 0, t_ms, &cmd); t_ms += 20u; }
    if (lift) ui_touch(&ui, 0, pts[n - 1][0], pts[n - 1][1], 0, 0, t_ms, &cmd);
}

// along a circle round the scope centre, from angle a0 to a1 (degrees, 0 up, clockwise), radius r0 to r1
static int path(int (*pts)[2], float a0, float a1, float r0, float r1, int n)
{
    for (int k = 0; k < n; k++) {
        float f = (float)k / (float)(n - 1), a = (a0 + (a1 - a0) * f) * D2R, r = r0 + (r1 - r0) * f;
        pts[k][0] = (int)lroundf(SCX + r * sinf(a));
        pts[k][1] = (int)lroundf(SCY - r * cosf(a));
    }
    return n;
}

static int find(const char *flight)
{
    for (int i = 0; i < sky.n; i++) if (!strcmp(sky.p[i].flight, flight)) return i;
    return -1;
}

static void pick(const char *flight)
{
    int i = find(flight);
    ui.sel = i >= 0 ? sky.p[i].icao : 0;
    if (i < 0) printf("  %s is not in the sky now\n", flight);
}

// HUE LIST SET: the panel's buttons, or on the round glass the row a tap on nothing brings up
static void press(int i)
{
#if RADAR_ROUND
    ui.menu_until = t_ms + ROUND_MENU_MS;
    int m = i ? i + 1 : 0;                           // the row has the look's button second
    tap(menu_x(m) + MENU_W / 2, MENU_Y + MENU_H / 2);
#else
    tap(TX - 3 + i * (BTN_W + 3) + BTN_W / 2, BTN_Y + 10);
#endif
}

// the bottom bar of a page: n buttons, press the i-th
static void press_bar(int i, int n)
{
    int w = (BAR_R - BAR_L - 8 - (n - 1) * 4) / n;
    tap(BAR_L + 4 + i * (w + 4) + w / 2, BAR_Y + BAR_H / 2);
}
```

# tools/preview/png.inc: a PNG writer with its own deflate, no zlib

```c

/* ---- tiny PNG writer with its own deflate, so no zlib needed ---- */
static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    c ^= 0xFFFFFFFFu;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc(0, (const uint8_t *)type, 4);
    c = crc(c, data, len);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* deflate with the fixed huffman table (RFC 1951 3.2.6) and plain LZ77, hash chains.
 * not zlib grade, but screenshots shrink 5-20x and it's 80 lines instead of a library */
typedef struct { uint8_t *p; uint32_t bits; int nb; } bitw_t;

static void put_bits(bitw_t *w, uint32_t v, int n)
{
    w->bits |= v << w->nb;
    w->nb += n;
    while (w->nb >= 8) { *w->p++ = (uint8_t)w->bits; w->bits >>= 8; w->nb -= 8; }
}

// huffman codes go out MSB first, the rest of the stream LSB first
static void put_code(bitw_t *w, uint32_t code, int len)
{
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    put_bits(w, r, len);
}

static void put_sym(bitw_t *w, int v)
{
    if (v < 144) put_code(w, 0x30u + v, 8);
    else if (v < 256) put_code(w, 0x190u + (v - 144), 9);
    else if (v < 280) put_code(w, (uint32_t)(v - 256), 7);
    else put_code(w, 0xC0u + (v - 280), 8);
}

static const uint16_t LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                       67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t  LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t  DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static size_t deflate_fixed(const uint8_t *in, size_t n, uint8_t *out)
{
    enum { WIN = 32768, HBITS = 15, CHAIN = 48 };
    int32_t *head = malloc(sizeof(int32_t) << HBITS), *prev = malloc(sizeof(int32_t) * WIN);
    for (int i = 0; i < 1 << HBITS; i++) head[i] = -1;
    bitw_t w = { out, 0, 0 };
    put_bits(&w, 1, 1);                           // last block
    put_bits(&w, 1, 2);                           // fixed huffman
    size_t i = 0;
    while (i < n) {
        int best = 0, dist = 0;
        if (i + 2 < n) {
            uint32_t h = ((uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2]) * 2654435761u >> (32 - HBITS);
            int32_t c = head[h];
            for (int k = 0; k < CHAIN && c >= 0 && i - (size_t)c <= WIN - 1; k++, c = prev[c % WIN]) {
                int l = 0, lim = n - i < 258 ? (int)(n - i) : 258;
                while (l < lim && in[c + l] == in[i + l]) l++;
                if (l > best) { best = l; dist = (int)(i - (size_t)c); if (l == lim) break; }
            }
            prev[i % WIN] = head[h];
            head[h] = (int32_t)i;
        }
        if (best >= 3) {
            int li = 28;
            while (LEN_BASE[li] > best) li--;
            put_sym(&w, 257 + li);
            put_bits(&w, (uint32_t)(best - LEN_BASE[li]), LEN_EXTRA[li]);
            int di = 29;
            while (DIST_BASE[di] > dist) di--;
            put_code(&w, (uint32_t)di, 5);
            put_bits(&w, (uint32_t)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
            // the skipped bytes go into the hash too, or later matches are missed
            for (size_t j = i + 1; j < i + (size_t)best && j + 2 < n; j++) {
                uint32_t h = ((uint32_t)in[j] << 16 | (uint32_t)in[j + 1] << 8 | in[j + 2]) * 2654435761u >> (32 - HBITS);
                prev[j % WIN] = head[h];
                head[h] = (int32_t)j;
            }
            i += (size_t)best;
        } else {
            put_sym(&w, in[i++]);
        }
    }
    put_sym(&w, 256);
    if (w.nb) put_bits(&w, 0, 8 - w.nb);
    free(head); free(prev);
    return (size_t)(w.p - out);
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    // every row gets the filter that leaves the smallest numbers (the usual trick)
    const size_t stride = (size_t)w * 3;
    size_t raw_len = (size_t)h * (1 + stride);
    uint8_t *raw = malloc(raw_len), *tmp = malloc(stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *cur = rgb + y * stride, *up = y ? cur - stride : 0;
        uint8_t *dst = raw + y * (1 + stride);
        long best_sum = -1;
        for (int f = 0; f < 5; f++) {
            if (f >= 2 && !up) break;
            long sum = 0;
            for (size_t x = 0; x < stride; x++) {
                int a = x >= 3 ? cur[x - 3] : 0, b = up ? up[x] : 0, c = (x >= 3 && up) ? up[x - 3] : 0;
                int pred = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
                tmp[x] = (uint8_t)(cur[x] - pred);
                sum += tmp[x] < 128 ? tmp[x] : 256 - tmp[x];
            }
            if (best_sum < 0 || sum < best_sum) { best_sum = sum; dst[0] = (uint8_t)f; memcpy(dst + 1, tmp, stride); }
        }
    }
    free(tmp);
    uint8_t *z = malloc(raw_len + raw_len / 4 + 64), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    p += deflate_fixed(raw, raw_len, p);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    be32(p, (b << 16) | a);
    p += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return 0; }
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(p - z));
    chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    return 1;
}
```

# Air Quality tools/preview/preview.c: --prof, proportions on the PC

```c
/* --prof: how long each part of the home page takes to draw, on this PC. only the proportions
 * mean anything for the ESP32, the absolute numbers don't */
#include <time.h>
extern int g_prof_off;                                  /* ui_home.c: bit 0 air HUD, 1 pressure HUD, 2 cards */
static double ms_of(int frames_n)
{
    clock_t t0 = clock();
    for (int i = 0; i < frames_n; i++) {
        T += 50;
        feed();
        ui_frame(&ui, T);
        for (int y = 0; y < GFX_H; y += STRIP) {
            canvas_t cv = { strip_px, y, y + STRIP <= GFX_H ? STRIP : GFX_H - y };
            ui_strip(&ui, &cv, scratch);
            gfx_pack565(&cv, frame565 + y * GFX_W);
        }
    }
    return (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / frames_n;
}

static void prof(void)
{
    start(1, 18.0f, 7);
    run(86400);
    frames(40);
    const int N = 200;
    double all = ms_of(N);
    g_prof_off = 1; double no_air = ms_of(N);
    g_prof_off = 2; double no_pres = ms_of(N);
    g_prof_off = 4; double no_cards = ms_of(N);
    g_prof_off = 7; double bare = ms_of(N);
    g_prof_off = 0;
    printf("home frame %.2f ms: air HUD %.2f, pressure HUD %.2f, cards %.2f, the rest (background, tabs, pack) %.2f\n",
           all, all - no_air, all - no_pres, all - no_cards, bare);
}

```

# tools/preview/TOUR.BAT

```bat
@echo off
rem the how-to film for the site: tour.c plays touches on the screen code, ffmpeg makes the MP4
rem   TOUR.BAT              the 3.5" CYD (480x320, the board in the site's photo)
rem   TOUR.BAT ili9341      another screen (ili9341 st7796)
rem ffmpeg from C:\ffmpeg\bin, or set FFMPEG=path\ffmpeg.exe
rem writes ..\..\shots\<screen>\tour.mp4, its picture tour.png and its captions tour.vtt
setlocal
set DISP=%~1
if "%DISP%"=="" set DISP=st7796
set NUM=
if /i "%DISP%"=="ili9341" set NUM=1
if /i "%DISP%"=="st7796"  set NUM=2
if "%NUM%"=="" (
  echo the film is made for ili9341 or st7796, not %DISP%
  exit /b 1
)
set OUT=..\..\shots\%DISP%
if "%FFMPEG%"=="" set FFMPEG=C:\ffmpeg\bin\ffmpeg.exe
if not exist "%FFMPEG%" (
  echo ffmpeg not found: %FFMPEG%
  exit /b 1
)
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ tools not found: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist build\tour_%DISP% mkdir build\tour_%DISP%
rem the map's tiles from OpenStreetMap, fetched once by curl (ground.inc)
if not exist tiles mkdir tiles
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% ^
   /Fobuild\tour_%DISP%\ /Febuild\tour_%DISP%\tour.exe ^
   tour.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ..\..\core\ui_scope.c ..\..\core\ui_pages.c ..\..\core\ui_wifi.c ..\..\core\ui_about.c ^
   ..\..\core\qr_site.c ..\..\core\map.c ..\..\core\png_decode.c ..\..\core\watch.c ..\..\core\light.c ^
   ..\..\core\font_small.c ..\..\core\font_medium.c ..\..\core\font_large.c ..\..\core\font_huge.c
if errorlevel 1 exit /b 1
if not exist %OUT% mkdir %OUT%
build\tour_%DISP%\tour.exe "%FFMPEG%" %OUT%\tour.mp4 %OUT%\tour.png %OUT%\tour.vtt
```

# tools/preview/tour.c: the scripted film

```c
/*
 * tour - the how-to film for the site: the flight radar's own screen code (core/, the same as in
 * the firmware) plays a script of touches over the demo sky and a finger is drawn where it touches.
 * frames go to ffmpeg as raw RGB, out comes an MP4 of the screen alone (the site puts it into a
 * photo of the board); what each part shows goes to a WebVTT file the page reads under it.
 * usage: tour <ffmpeg.exe> <out.mp4> <poster.png> <captions.vtt>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/sky.h"
#include "../../core/demo.h"
#include "../../core/ui.h"
#include "../../core/ui_int.h"

#include "png.inc"
#include "ground.inc"

#define HOME_LAT 48.8945f              // Trencin, the board's home till the site sends one
#define HOME_LON 18.0444f
#define FPS      25
#define FRAME_MS (1000 / FPS)
#define Z        2                                  /* screen pixels are 2x2 in the film */
#define OUT_W    (GFX_W * Z)
#define OUT_H    (GFX_H * Z)

static sky_t    sky;
static demo_t   demo;
static ui_t     ui;
static uint32_t t_ms;
static char     json[32768];
static FILE    *film;
static uint8_t  rgb[OUT_W * OUT_H * 3];
static int      frames;
static const char *poster;

/* the finger: where it is, whether it presses, and the last lift for a ripple */
static float    fx = -100, fy = -100;
static int      f_down, f_shown;
static uint32_t f_lift = 0;
/* the captions: what each part shows, from when. written out as WebVTT at the end */
typedef struct { uint32_t at; const char *a, *b; } cue_t;
static cue_t    cues[32];
static int      ncues;
static uint32_t t_start;

static void feed_sky(void)
{
    int n = demo_json(&demo, HOME_LAT, HOME_LON, range_km(&ui.cfg), t_ms, json, sizeof json);
    static sky_reader_t rd;
    sky_read_begin(&rd, &sky, 0, t_ms);
    sky_read(&rd, json, n);
    sky_read_end(&rd, &sky);
    sky_forget(&sky, t_ms);
}

static void tick(void)
{
    uint32_t was = t_ms;
    t_ms += FRAME_MS;
    if (t_ms / 5000u != was / 5000u) feed_sky();     // an answer every 5 s, like the board asks
}

/* --- drawing a frame --- */

#define STRIP 24
static uint32_t strip_px[GFX_W * STRIP];
static uint16_t screen565[GFX_W * GFX_H];

static void put565(int y0, int h)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = 0; x < GFX_W; x++) {
            uint16_t v = screen565[y * GFX_W + x];
            v = (uint16_t)((v >> 8) | (v << 8));
            uint8_t r = (uint8_t)(((v >> 11) & 31) * 255 / 31), g = (uint8_t)(((v >> 5) & 63) * 255 / 63), b = (uint8_t)((v & 31) * 255 / 31);
            for (int dy = 0; dy < Z; dy++)
                for (int dx = 0; dx < Z; dx++) {
                    uint8_t *o = rgb + ((size_t)(y * Z + dy) * OUT_W + x * Z + dx) * 3;
                    o[0] = r; o[1] = g; o[2] = b;
                }
        }
}

// a fingertip seen from above: a soft light disc, firmer when it presses, a ripple when it lets go
static void finger_overlay(void)
{
    if (!f_shown) return;
    const float cx = fx * Z + Z / 2.0f, cy = fy * Z + Z / 2.0f, R = 15.0f;
    float rip = (float)(t_ms - f_lift) / 350.0f;
    for (int y = (int)(cy - 40); y <= (int)(cy + 40); y++) {
        if (y < 0 || y >= GFX_H * Z) continue;
        for (int x = (int)(cx - 40); x <= (int)(cx + 40); x++) {
            if (x < 0 || x >= OUT_W) continue;
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)), a = 0;
            a += (f_down ? 0.34f : 0.16f) * clampf(R - d, 0, 1);                  // the tip
            a += (f_down ? 0.85f : 0.55f) * clampf(1.2f - fabsf(d - R), 0, 1);    // its edge
            if (f_down) a += 0.5f * clampf(4.5f - d, 0, 1);                       // where it presses
            if (!f_down && rip < 1.0f) a += 0.7f * (1.0f - rip) * clampf(1.5f - fabsf(d - (R + 22.0f * rip)), 0, 1);
            if (a <= 0) continue;
            a = clampf(a, 0, 1);
            uint8_t *o = rgb + ((size_t)y * OUT_W + x) * 3;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] + (255 - o[k]) * a);
        }
    }
}

static void frame(void)
{
    if (!ui.t_down) make_ground(&ui);             // like the board: made again once the finger is off, stretched till then
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    for (int y = 0; y < GFX_H; y += STRIP) {
        canvas_t cv = { strip_px, y, y + STRIP <= GFX_H ? STRIP : GFX_H - y, 0 };
        ui_strip(&ui, &cv);
        gfx_pack565(&cv, screen565 + y * GFX_W);
    }
    put565(0, GFX_H);
    finger_overlay();
    fwrite(rgb, 1, sizeof rgb, film);
    if (frames++ == FPS * 2 && poster) write_png(poster, rgb, OUT_W, OUT_H);
    tick();
}

/* --- the script's verbs --- */

static ui_cmd_t cmd;

static void touch(int down)
{
    ui.snap = sky;
    ui_frame(&ui, t_ms);
    ui_touch(&ui, down, (int)lroundf(fx), (int)lroundf(fy), 0, 0, t_ms, &cmd);
}

static void caption(const char *a, const char *b)
{
    if (ncues < (int)(sizeof cues / sizeof cues[0])) cues[ncues++] = (cue_t){ t_ms - t_start, a, b };
}

static void vtt_time(FILE *f, uint32_t ms)
{
    fprintf(f, "%02u:%02u:%02u.%03u", ms / 3600000u, ms / 60000u % 60u, ms / 1000u % 60u, ms % 1000u);
}

static void write_vtt(const char *path, uint32_t end)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("could not write %s\n", path); return; }
    fprintf(f, "WEBVTT\n\n");
    for (int i = 0; i < ncues; i++) {
        fprintf(f, "%d\n", i + 1);
        vtt_time(f, cues[i].at);
        fprintf(f, " --> ");
        vtt_time(f, i + 1 < ncues ? cues[i + 1].at : end);
        fprintf(f, "\n%s\n%s\n\n", cues[i].a, cues[i].b);
    }
    fclose(f);
}

static void wait(int ms) { for (int t = 0; t < ms; t += FRAME_MS) frame(); }

static float ease(float f) { return f * f * (3.0f - 2.0f * f); }

// the finger, up, glides to x,y. a plane moves meanwhile: a target can be followed by giving -1 and a flight
static int plane_spot(const char *flight, float *x, float *y)
{
    for (int i = 0; i < sky.n; i++)
        if (!strcmp(sky.p[i].flight, flight) && ui.spot[i].vis == 1) { *x = ui.spot[i].x; *y = ui.spot[i].y; return 1; }
    for (int k = 0; k < ui.n_order; k++) {             // not on the scope now: the nearest one that is
        const spot_t *sp = &ui.spot[ui.order[k]];
        if (sp->vis == 1) { *x = sp->x; *y = sp->y; return 1; }
    }
    return 0;
}

static void glide(float x, float y, int ms, const char *follow)
{
    float x0 = fx, y0 = fy;
    if (!f_shown) { x0 = x + 40; y0 = GFX_H + 20; f_shown = 1; }
    for (int t = 0; t < ms; t += FRAME_MS) {
        float tx = x, ty = y;
        if (follow) { ui.snap = sky; ui_frame(&ui, t_ms); plane_spot(follow, &tx, &ty); }
        float f = ease((float)(t + FRAME_MS) / (float)ms);
        fx = x0 + (tx - x0) * f;
        fy = y0 + (ty - y0) * f;
        frame();
    }
}

static void tap(void)
{
    f_down = 1;
    for (int k = 0; k < 4; k++) { touch(1); frame(); }   // 160 ms: a tap, not a hold
    f_down = 0;
    touch(0);
    f_lift = t_ms;
    frame();
}

// pressed, moved along the scope from (a0, r0) to (a1, r1) in polar steps round home, lifted
static void drag_polar(float a0, float a1, float r0, float r1, int ms)
{
    f_down = 1;
    int n = ms / FRAME_MS;
    for (int k = 0; k <= n; k++) {
        float f = ease((float)k / (float)n), a = (a0 + (a1 - a0) * f) * D2R, r = r0 + (r1 - r0) * f;
        fx = SCX + r * sinf(a);
        fy = SCY - r * cosf(a);
        touch(1);
        frame();
    }
    for (int k = 0; k < 6; k++) { touch(1); frame(); }   // a moment still, then let go
    f_down = 0;
    touch(0);
    f_lift = t_ms;
    frame();
}

static void drag_line(float x1, float y1, int ms)
{
    float x0 = fx, y0 = fy;
    f_down = 1;
    int n = ms / FRAME_MS;
    for (int k = 0; k <= n; k++) {
        float f = ease((float)k / (float)n);
        fx = x0 + (x1 - x0) * f;
        fy = y0 + (y1 - y0) * f;
        touch(1);
        frame();
    }
    f_down = 0;
    touch(0);
    f_lift = t_ms;
    frame();
}

static float polar_x(float a, float r) { return SCX + r * sinf(a * D2R); }
static float polar_y(float a, float r) { return SCY - r * cosf(a * D2R); }
static float btn_cx(int i) { return (float)(TX - 3 + i * (BTN_W + 3) + BTN_W / 2); }
static float btn_cy(void) { return (float)(BTN_Y + BTN_H / 2); }

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: tour <ffmpeg.exe> <out.mp4> [poster.png]\n"); return 1; }
    poster = argv[3];
    crc_init();
    char line[1024];
    snprintf(line, sizeof line, "%s -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
             "-c:v libx264 -preset slow -crf 24 -tune animation -pix_fmt yuv420p -movflags +faststart \"%s\"",
             argv[1], OUT_W, OUT_H, FPS, argv[2]);
    film = _popen(line, "wb");
    if (!film) { printf("could not start %s\n", argv[1]); return 1; }

    ui_init(&ui, 0);
    sky_init(&sky, HOME_LAT, HOME_LON);
    demo_init(&demo, range_km(&ui.cfg), 82);
    ui.net.configured = ui.net.wifi = 1;               // a board with its settings, on the WiFi
    snprintf(ui.net.ssid, sizeof ui.net.ssid, "HOME");
    snprintf(ui.net.server, sizeof ui.net.server, "ADSB.LOL");
    for (int s = 0; s < 90 * FPS; s++) tick();       // ninety seconds of sky first: trails grow
    t_start = t_ms;
    const float RIM = SR - 7.0f, R_IN = PR * 0.45f, R_OUT = PR * 0.9f;   // where the finger turns and zooms

    caption("THE PLANES AROUND YOUR HOME", "home in the middle, each one turned the way it flies");
    wait(3500);

    caption("TAP A PLANE", "its callsign, type, height, speed and how far it is");
    glide(0, 0, 900, "OWL77H");
    tap();
    wait(3000);
    caption("NEXT: THE NEXT NEAREST ONE", "X lets it go");
    glide(btn_cx(0), btn_cy(), 700, NULL);
    tap();
    wait(1800);
    glide(btn_cx(2), btn_cy(), 600, NULL);
    tap();
    wait(800);

    caption("DRAG THE RIM: THE MAP TURNS", "match it to the way the screen stands");
    glide(polar_x(0, RIM), polar_y(0, RIM), 900, NULL);
    drag_polar(0, 80, RIM, RIM, 1800);
    wait(1500);
    caption("BACK NEAR NORTH IT SNAPS", "north up again");
    glide(polar_x(80, RIM), polar_y(80, RIM), 500, NULL);
    drag_polar(80, 3, RIM, RIM, 1500);
    wait(1400);

    caption("DRAG INSIDE: ZOOM", "away from home: closer");
    glide(polar_x(225, R_IN), polar_y(225, R_IN), 900, NULL);
    drag_polar(225, 225, R_IN, R_OUT, 1600);
    wait(1800);
    caption("DRAG INSIDE: ZOOM", "toward home: farther");
    drag_polar(225, 225, R_OUT, R_IN, 1600);
    wait(1500);

    caption("HUE: FIVE COLOUR THEMES", "the same meaning in all of them: colour is height");
    glide(btn_cx(0), btn_cy(), 900, NULL);
    for (int k = 0; k < UI_THEMES; k++) { tap(); wait(k == UI_THEMES - 1 ? 2300 : 1300); }   // the last name fades first

    caption("THE LOOK: RADAR, MAP, CLASSIC", "the beam over the map, a square map north up, and in the map's own colours");
    for (int k = 0; k < LOOKS; k++) {                // the button moves with the look: the finger follows it
        glide(look_x(&ui) + look_w(&ui) / 2.0f, LOOK_Y + LOOK_H / 2.0f, k ? 600 : 900, NULL);
        tap();
        wait(k == LOOKS - 1 ? 1200 : 2400);          // round to the radar again
        if (k == 0) {
            caption("THE THREE DOTS: THE PANEL IN AND OUT", "the map has the whole screen, a picked plane brings the panel");
            glide(GFX_W - 18.0f, LOOK_Y + LOOK_H / 2.0f, 600, NULL);
            tap();
            wait(2000);
            tap();
            wait(1000);
            caption("THE LOOK: RADAR, MAP, CLASSIC", "the beam over the map, a square map north up, and in the map's own colours");
        }
    }

    caption("LIST: EVERY FLIGHT, NEAREST FIRST", "drag to scroll");
    glide(btn_cx(1), btn_cy(), 700, NULL);
    tap();
    glide(GFX_W / 2.0f, GFX_H * 0.66f, 600, NULL);  // off the BACK that is under it now
    wait(900);
    drag_line(GFX_W / 2.0f, GFX_H * 0.33f, 900);
    wait(700);
    caption("TAP A ROW", "and it is picked on the scope");
    glide(60, GFX_H * 0.46f, 600, NULL);
    tap();
    wait(2500);
    glide(btn_cx(2), btn_cy(), 700, NULL);
    tap();
    wait(600);

    caption("SET: HOW IT SHOWS", "units, labels, map up, the screen upside down");
    glide(btn_cx(2), btn_cy(), 500, NULL);
    tap();
    wait(1500);
    glide(GFX_W / 2.0f, GFX_H * 0.7f, 600, NULL);
    drag_line(GFX_W / 2.0f, GFX_H * 0.25f, 1100);
    wait(1500);
    glide(GFX_W / 2.0f, (float)(BAR_Y + BAR_H / 2), 700, NULL);
    tap();
    caption("THE PLANES AROUND YOUR HOME", "home in the middle, each one turned the way it flies");
    glide(GFX_W + 40.0f, GFX_H + 30.0f, 900, NULL);
    f_shown = 0;
    wait(1500);

    write_vtt(argv[4], t_ms - t_start);
    int rc = _pclose(film);
    printf("tour: %d frames, %.1f s, ffmpeg %d -> %s\n", frames, frames / (double)FPS, rc, argv[2]);
    return rc;
}
```

# tools/preview/GIF.BAT

```bat
@echo off
rem the README's GIF: the how-to film (TOUR.BAT st7796 first) inside the photo of the 3.5" board,
rem two parts of it: the planes, a tap, the rim and the zoom; then the looks and the three dots.
rem 8 frames a second keeps it near 4 MB. ffmpeg from C:\ffmpeg\bin, or set FFMPEG=path\ffmpeg.exe
rem writes ..\..\shots\st7796\tour.gif
setlocal
if "%FFMPEG%"=="" set FFMPEG=C:\ffmpeg\bin\ffmpeg.exe
if not exist "%FFMPEG%" (
  echo ffmpeg not found: %FFMPEG%
  exit /b 1
)
cd /d "%~dp0..\.."
rem the photo is 700x400 with the screen at 16%% 10%%, 71.5%% wide: here 600 wide
"%FFMPEG%" -hide_banner -loglevel error -y -f lavfi -i "color=c=0x03081a:s=600x342:r=8" ^
  -i site\img\board-35.png -i shots\st7796\tour.mp4 -filter_complex ^
  "[2:v]split[v1][v2];[v1]trim=0:24.9,setpts=PTS-STARTPTS[c1];[v2]trim=37.8:45.4,setpts=PTS-STARTPTS[c2];[c1][c2]concat=n=2:v=1[cv];[1:v]scale=600:342[ph];[0:v][ph]overlay=0:0[bg];[cv]fps=8,scale=428:286:flags=lanczos[s];[bg][s]overlay=96:34:shortest=1,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle" ^
  -loop 0 shots\st7796\tour.gif
if errorlevel 1 exit /b 1
echo wrote shots\st7796\tour.gif
```

# site/index.html: the captions read, not shown

```html
       (tools/preview/tour.c). what each part shows comes from the film's captions, under the board -->
  <section class="hero">
    <div class="board">
      <img class="board-photo" src="img/board-35.png" alt="an ESP32 board with a 3.5 inch screen">
      <video class="board-screen" src="shots/st7796/tour.mp4" poster="shots/st7796/tour.png"
             autoplay muted loop playsinline preload="metadata"
             aria-label="how to use it: tap a plane, drag the rim to turn the map, drag inside to zoom, HUE, LIST, SET">
        <track id="cues" kind="captions" src="shots/st7796/tour.vtt" srclang="en" label="English" default>
      </video>
    </div>
    <div class="cue" aria-live="polite"><b id="cue1">THE PLANES AROUND YOUR HOME</b><span id="cue2">home in the middle, each one turned the way it flies</span></div>
```

```js
/* the film's captions go under the board, not over the picture: the track is read, not shown */
{
  const track = $('cues').track;
  track.mode = 'hidden';
  track.oncuechange = () => {
    const c = track.activeCues && track.activeCues[0];
    if (!c) return;
    const [a, b] = c.text.split('\n');
    $('cue1').textContent = a;
    $('cue2').textContent = b || '';
  };
}

```

# tools/Guide/GUIDE.BAT and the format of notes.txt

```bat
@echo off
rem the pictures of where to tap (shots\guide\*.png), from the preview's shots and notes.txt
rem run tools\preview\BUILD.BAT for st7796, gc9a01 and ili9341 first. needs the .NET 8 SDK
setlocal
cd /d "%~dp0"
dotnet run -c Release -- notes.txt ..\..\shots
```

```text
# where to tap, for the site's step 3 and the README. made by GUIDE.BAT after tools/preview
#   image <shot under shots/> <picture written to shots/guide/>
#   <L|R> <x> <y> <words>            a spot to tap or drag, ringed
#   <L|R> <x> <y> <w>x<h> <words>    a button, framed: x y its top left corner
# all in the screen's own pixels. '|' breaks the words into lines

# the 3.5" board, the radar look
image st7796/02-live.png radar.png
L 49 55 DRAG THE RIM:|THE MAP TURNS
L 145 240 TAP A PLANE
L 215 120 DRAG INSIDE:|ZOOM
L 6 300 144x18 COLOUR IS HEIGHT
R 247 2 52x20 THE LOOK:|RADAR, MAP, CLASSIC
R 312 72 160x15 TAP A ROW:|THE SAME PLANE
R 311 282 162x32 HUE: COLOURS|LIST: EVERY FLIGHT|SET: SETTINGS
```

# tools/Guide/Program.cs

```csharp
// Guide - the pictures of where to tap, for the site and the README: a screenshot from
// tools/preview with short words at its sides and an arrow from each to its place on the screen
//   Guide <notes.txt> <shots folder>
// notes.txt: "image <shot> <out>" starts a picture (the shot as the preview writes it, twice the
// screen's size; out goes to <shots>/guide), then a line a note: "<L|R> <x> <y> <words>" rings a
// spot, "<L|R> <x> <y> <w>x<h> <words>" frames a button, its top left corner at x y. all in the
// screen's own pixels. the words go to the left or the right side, '|' breaks a line
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Linq;

static class Program
{
    const int Zoom = 2;          // the preview's shots are twice the screen
    const int Side = 300;        // room for the words on each side of the shot
    const int Pad = 28;          // above and below it
    const int Gap = 10;          // between two notes on one side
    const int Ring = 15;         // the circle round the place to tap

    static readonly Color Back = Color.FromArgb(3, 8, 26);
    static readonly Color Frame = Color.FromArgb(29, 74, 120);
    static readonly Color Card = Color.FromArgb(240, 11, 33, 66);
    static readonly Color Ink = Color.FromArgb(234, 247, 255);
    static readonly Color Mark = Color.FromArgb(255, 184, 72);
    static readonly Color Shade = Color.FromArgb(170, 3, 8, 26);

    sealed class Note
    {
        public bool Left;
        public int X, Y, W, H;           // W 0: a spot, ringed. else a button, framed
        public string[] Lines = Array.Empty<string>();
        public RectangleF Box;
        public float MidY => W > 0 ? Y + H / 2f : Y;
    }

    static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: Guide <notes.txt> <shots folder>");
            return 2;
        }
        string shots = Path.GetFullPath(args[1]);
        string outDir = Path.Combine(shots, "guide");
        Directory.CreateDirectory(outDir);
        string? shot = null, outName = null;
        List<Note> notes = new List<Note>();
        int made = 0;
        foreach (string raw in File.ReadAllLines(args[0]))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal))
            {
                continue;
            }
            string[] w = line.Split(' ', 4, StringSplitOptions.RemoveEmptyEntries);
            if (w[0] == "image")
            {
                if (shot != null && outName != null)
                {
                    Render(Path.Combine(shots, shot), Path.Combine(outDir, outName), notes);
                    made++;
                }
                shot = w[1];
                outName = w[2];
                notes = new List<Note>();
                continue;
            }
            if (w.Length < 4 || (w[0] != "L" && w[0] != "R"))
            {
                Console.Error.WriteLine("not a note: " + line);
                return 1;
            }
            Note note = new Note
            {
                Left = w[0] == "L",
                X = int.Parse(w[1], CultureInfo.InvariantCulture),
                Y = int.Parse(w[2], CultureInfo.InvariantCulture),
            };
            string words = w[3];
            string[] size = words.Split(' ', 2)[0].Split('x');
            if (size.Length == 2 && int.TryParse(size[0], out int bw) && int.TryParse(size[1], out int bh))
            {
                note.W = bw;
                note.H = bh;
                words = words.Split(' ', 2)[1];
            }
            note.Lines = words.Split('|');
            notes.Add(note);
        }
        if (shot != null && outName != null)
        {
            Render(Path.Combine(shots, shot), Path.Combine(outDir, outName), notes);
            made++;
        }
        Console.WriteLine(made + " pictures in " + outDir);
        return 0;
    }

    static void Render(string shotPath, string outPath, List<Note> notes)
    {
        using Bitmap shot = new Bitmap(shotPath);
        int w = Side + shot.Width + Side, h = shot.Height + 2 * Pad;
        using Bitmap pic = new Bitmap(w, h, PixelFormat.Format24bppRgb);
        using Graphics g = Graphics.FromImage(pic);
        g.Clear(Back);
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.DrawImage(shot, new Rectangle(Side, Pad, shot.Width, shot.Height));
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        using (Pen frame = new Pen(Frame, 2))
        {
            g.DrawRectangle(frame, Side - 1, Pad - 1, shot.Width + 1, shot.Height + 1);
        }

        using Font font = new Font("Consolas", 21, FontStyle.Bold, GraphicsUnit.Pixel);
        StringFormat fmt = StringFormat.GenericTypographic;
        float lineH = font.GetHeight(g) + 2;
        foreach (Note n in notes)
        {
            float tw = n.Lines.Max(s => g.MeasureString(s, font, 1000, fmt).Width);
            float bw = tw + 28, bh = n.Lines.Length * lineH + 16;
            float bx = n.Left ? Side - 22 - bw : Side + shot.Width + 22;
            n.Box = new RectangleF(bx, 0, bw, bh);
        }
        Place(notes.Where(n => n.Left).ToList(), h);
        Place(notes.Where(n => !n.Left).ToList(), h);

        // the arrows first, the words over them
        foreach (Note n in notes)
        {
            PointF from = new PointF(n.Left ? n.Box.Right : n.Box.Left, n.Box.Top + n.Box.Height / 2);
            if (n.W == 0)
            {
                Arrow(g, from, new PointF(Side + n.X * Zoom, Pad + n.Y * Zoom), Ring + 3);
                Circle(g, Side + n.X * Zoom, Pad + n.Y * Zoom);
                continue;
            }
            // a button: a frame round it, the arrow to the nearest point of the frame
            RectangleF f = new RectangleF(Side + n.X * Zoom - 5, Pad + n.Y * Zoom - 5, n.W * Zoom + 10, n.H * Zoom + 10);
            PointF to = new PointF(Math.Clamp(from.X, f.Left, f.Right), Math.Clamp(from.Y, f.Top, f.Bottom));
            Arrow(g, from, to, 3);
            Outline(g, f);
        }
        using (SolidBrush card = new SolidBrush(Card))
        using (SolidBrush ink = new SolidBrush(Ink))
        using (Pen edge = new Pen(Mark, 2))
        {
            foreach (Note n in notes)
            {
                using (GraphicsPath box = Rounded(n.Box, 8))
                {
                    g.FillPath(card, box);
                    g.DrawPath(edge, box);
                }
                for (int i = 0; i < n.Lines.Length; i++)
                {
                    g.DrawString(n.Lines[i], font, ink, n.Box.Left + 14, n.Box.Top + 8 + i * lineH, fmt);
                }
            }
        }
        pic.Save(outPath, ImageFormat.Png);
        Console.WriteLine("wrote " + outPath);
    }

    // a side's notes top to bottom, each as near its place's height as the ones above it let it be
    static void Place(List<Note> side, int h)
    {
        side.Sort((a, b) => a.MidY.CompareTo(b.MidY));
        float below = 4;
        foreach (Note n in side)
        {
            float want = Pad + n.MidY * Zoom - n.Box.Height / 2;
            float top = Math.Max(want, below);
            n.Box = new RectangleF(n.Box.X, top, n.Box.Width, n.Box.Height);
            below = top + n.Box.Height + Gap;
        }
        float limit = h - 4;                            // pushed off the bottom: back up, the ones above too
        for (int i = side.Count - 1; i >= 0; i--)
        {
            Note n = side[i];
            if (n.Box.Bottom > limit)
            {
                n.Box = new RectangleF(n.Box.X, limit - n.Box.Height, n.Box.Width, n.Box.Height);
            }
            limit = n.Box.Top - Gap;
        }
    }

    // a line from the words to the place, its head short of it by stop. a dark edge under the
    // line, the circle and the frame keeps them seen over the light map
    static void Arrow(Graphics g, PointF from, PointF to, float stop)
    {
        float dx = to.X - from.X, dy = to.Y - from.Y, len = (float)Math.Sqrt(dx * dx + dy * dy);
        if (len < stop + 20)
        {
            return;
        }
        float ux = dx / len, uy = dy / len;
        PointF end = new PointF(to.X - ux * stop, to.Y - uy * stop);
        PointF[] head =
        {
            end,
            new PointF(end.X - ux * 16 - uy * 8, end.Y - uy * 16 + ux * 8),
            new PointF(end.X - ux * 16 + uy * 8, end.Y - uy * 16 - ux * 8),
        };
        PointF neck = new PointF(end.X - ux * 12, end.Y - uy * 12);
        using (Pen shade = new Pen(Shade, 8))
        {
            shade.LineJoin = LineJoin.Round;
            g.DrawLine(shade, from, neck);
            g.DrawPolygon(shade, head);
        }
        using (Pen mark = new Pen(Mark, 3))
        using (SolidBrush fill = new SolidBrush(Mark))
        {
            g.DrawLine(mark, from, neck);
            g.FillPolygon(fill, head);
        }
    }

    static void Circle(Graphics g, float x, float y)
    {
        using (Pen shade = new Pen(Shade, 8))
        {
            g.DrawEllipse(shade, x - Ring, y - Ring, 2 * Ring, 2 * Ring);
        }
        using (Pen mark = new Pen(Mark, 3))
        {
            g.DrawEllipse(mark, x - Ring, y - Ring, 2 * Ring, 2 * Ring);
        }
    }

    static void Outline(Graphics g, RectangleF f)
    {
        using (GraphicsPath p = Rounded(f, 8))
        {
            using (Pen shade = new Pen(Shade, 8))
            {
                g.DrawPath(shade, p);
            }
            using (Pen mark = new Pen(Mark, 3))
            {
                g.DrawPath(mark, p);
            }
        }
    }

    static GraphicsPath Rounded(RectangleF r, float rad)
    {
        GraphicsPath p = new GraphicsPath();
        float d = 2 * rad;
        p.AddArc(r.Left, r.Top, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Top, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.Left, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }
}
```

# tools/QrBake/Program.cs

```csharp
// QrBake - the site's address as a QR code for the board's ABOUT page
//   QrBake <url> <core folder>
// writes qr_site.h (the address, the size) and qr_site.c (the modules, a bit each). the board draws
// it with nothing to work out: the address does not change. run again when it does
using System;
using System.IO;
using System.Text;

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length < 2) {
            Console.WriteLine("QrBake <url> <core folder>");
            return 1;
        }
        string url = args[0], dir = args[1];
        bool[,] m = Qr.Encode(Encoding.UTF8.GetBytes(url), out int ver, out int mask);
        int n = m.GetLength(0), row = (n + 7) / 8;
        Console.WriteLine($"{url}: version {ver} ({n}x{n}), error correction M, mask {mask}");

        string made = $"written by tools/QrBake, don't edit: QrBake {url} core";
        StringBuilder h = new StringBuilder();
        h.Append("/* the site's address and its QR code, for ABOUT (ui_about.c) and for the board to ask the\n");
        h.Append($" * site for its version. {made} */\n");
        h.Append("#pragma once\n#include <stdint.h>\n\n");
        h.Append($"#define SITE_URL    \"{url}\"\n");
        h.Append($"#define QR_SITE_N   {n}                 /* modules a side: version {ver}, error correction M */\n");
        h.Append($"#define QR_SITE_ROW {row}                  /* bytes a row, the leftmost module in the top bit */\n");
        h.Append("extern const uint8_t QR_SITE[QR_SITE_N * QR_SITE_ROW];   /* 1: dark */\n");

        StringBuilder c = new StringBuilder();
        c.Append($"// {made}\n#include \"qr_site.h\"\n\n");
        c.Append("const uint8_t QR_SITE[QR_SITE_N * QR_SITE_ROW] = {\n");
        for (int y = 0; y < n; y++) {
            c.Append("   ");
            for (int b = 0; b < row; b++) {
                int v = 0;
                for (int k = 0; k < 8; k++) {
                    int x = b * 8 + k;
                    if (x < n && m[y, x]) {
                        v |= 0x80 >> k;
                    }
                }
                c.Append($" 0x{v:X2},");
            }
            c.Append("   // ");
            for (int x = 0; x < n; x++) {
                c.Append(m[y, x] ? '#' : '.');
            }
            c.Append('\n');
        }
        c.Append("};\n");

        File.WriteAllText(Path.Combine(dir, "qr_site.h"), h.ToString());
        File.WriteAllText(Path.Combine(dir, "qr_site.c"), c.ToString());
        Console.WriteLine($"wrote {Path.Combine(dir, "qr_site.h")} and qr_site.c");
        return 0;
    }
}
```

# tools/QrBake/Qr.cs: the encoder, byte mode, M correction

```csharp
// the QR encoder of OWLOS WiFi Radar's QrBake (byte mode, no library), with error correction M
// here: a code on a small screen wants big modules more than it wants to survive a picture in it
using System;
using System.Collections.Generic;

static class Qr
{
    // error correction M, versions 1..10: codewords per block, number of blocks (ISO 18004 table 9)
    static readonly int[] EccPerBlock = { -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
    static readonly int[] Blocks = { -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 };
    const int FormatBitsM = 0;                               // L 1, M 0, Q 3, H 2

    public static bool[,] Encode(byte[] data, out int version, out int mask)
    {
        // smallest version that holds it: mode (4 bits) + count (8 bits, versions 1-9) + the bytes
        version = 0;
        for (int v = 1; v <= 9; v++) {
            if (4 + 8 + data.Length * 8 <= DataCodewords(v) * 8) {
                version = v;
                break;
            }
        }
        if (version == 0) {
            throw new Exception("too long for versions 1-9");
        }

        List<bool> bits = new List<bool>();
        void Put(int val, int n)
        {
            for (int i = n - 1; i >= 0; i--) {
                bits.Add(((val >> i) & 1) != 0);
            }
        }
        Put(0b0100, 4);
        Put(data.Length, 8);
        foreach (byte b in data) {
            Put(b, 8);
        }
        int cap = DataCodewords(version) * 8;
        Put(0, Math.Min(4, cap - bits.Count));               // terminator
        Put(0, (8 - bits.Count % 8) % 8);
        for (int pad = 0xEC; bits.Count < cap; pad ^= 0xEC ^ 0x11) {
            Put(pad, 8);
        }
        byte[] cw = new byte[bits.Count / 8];
        for (int i = 0; i < bits.Count; i++) {
            if (bits[i]) {
                cw[i >> 3] |= (byte)(0x80 >> (i & 7));
            }
        }

        byte[] all = AddEcc(cw, version);
        int size = version * 4 + 17;
        bool[,] m = new bool[size, size];                     // [y, x]
        bool[,] function = new bool[size, size];
        DrawFunction(m, function, version);
        DrawCodewords(m, function, all);

        // try the 8 masks, keep the one with the lowest penalty
        int best = -1, bestPen = int.MaxValue;
        for (int k = 0; k < 8; k++) {
            ApplyMask(m, function, k);
            DrawFormat(m, function, k);
            int p = Penalty(m);
            if (p < bestPen) {
                bestPen = p;
                best = k;
            }
            ApplyMask(m, function, k);                         // xor again = undo
        }
        mask = best;
        ApplyMask(m, function, best);
        DrawFormat(m, function, best);
        return m;
    }

    static int RawModules(int v)
    {
        int r = (16 * v + 128) * v + 64;
        if (v >= 2) {
            int na = v / 7 + 2;
            r -= (25 * na - 10) * na - 55;
            if (v >= 7) {
                r -= 36;
            }
        }
        return r;
    }

    static int DataCodewords(int v) => RawModules(v) / 8 - EccPerBlock[v] * Blocks[v];

    static int Mul(int x, int y)                              // GF(2^8), x^8 + x^4 + x^3 + x^2 + 1
    {
        int z = 0;
        for (int i = 7; i >= 0; i--) {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        return z;
    }

    static byte[] Divisor(int degree)
    {
        byte[] r = new byte[degree];
        r[degree - 1] = 1;
        int root = 1;
        for (int i = 0; i < degree; i++) {
            for (int j = 0; j < r.Length; j++) {
                r[j] = (byte)Mul(r[j], root);
                if (j + 1 < r.Length) {
                    r[j] ^= r[j + 1];
                }
            }
            root = Mul(root, 0x02);
        }
        return r;
    }

    static byte[] Remainder(byte[] data, byte[] div)
    {
        byte[] r = new byte[div.Length];
        foreach (byte b in data) {
            int f = b ^ r[0];
            Array.Copy(r, 1, r, 0, r.Length - 1);
            r[^1] = 0;
            for (int i = 0; i < r.Length; i++) {
                r[i] ^= (byte)Mul(div[i], f);
            }
        }
        return r;
    }

    // split into blocks, error correction for each, interleave
    static byte[] AddEcc(byte[] data, int v)
    {
        int nb = Blocks[v], ecc = EccPerBlock[v], raw = RawModules(v) / 8;
        int nShort = nb - raw % nb, shortLen = raw / nb;
        byte[] div = Divisor(ecc);
        List<byte[]> blocks = new List<byte[]>();
        for (int i = 0, k = 0; i < nb; i++) {
            int len = shortLen - ecc + (i < nShort ? 0 : 1);
            byte[] dat = new byte[len];
            Array.Copy(data, k, dat, 0, len);
            k += len;
            byte[] e = Remainder(dat, div);
            byte[] blk = new byte[shortLen + 1];
            Array.Copy(dat, 0, blk, 0, len);
            Array.Copy(e, 0, blk, shortLen + 1 - ecc, ecc);   // short blocks keep a hole before their ecc
            blocks.Add(blk);
        }
        List<byte> res = new List<byte>();
        for (int i = 0; i < shortLen + 1; i++) {
            for (int j = 0; j < nb; j++) {
                if (i != shortLen - ecc || j >= nShort) {
                    res.Add(blocks[j][i]);
                }
            }
        }
        return res.ToArray();
    }

    static void Set(bool[,] m, bool[,] f, int x, int y, bool dark)
    {
        m[y, x] = dark;
        f[y, x] = true;
    }

    static void DrawFunction(bool[,] m, bool[,] f, int v)
    {
        int size = m.GetLength(0);
        for (int i = 0; i < size; i++) {
            Set(m, f, 6, i, i % 2 == 0);
            Set(m, f, i, 6, i % 2 == 0);
        }
        foreach ((int cx, int cy) in new[] { (3, 3), (size - 4, 3), (3, size - 4) }) {
            for (int dy = -4; dy <= 4; dy++) {
                for (int dx = -4; dx <= 4; dx++) {
                    int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= size || y >= size) {
                        continue;
                    }
                    int d = Math.Max(Math.Abs(dx), Math.Abs(dy));
                    Set(m, f, x, y, d != 2 && d != 4);
                }
            }
        }
        int[] pos = Align(v);
        for (int i = 0; i < pos.Length; i++) {
            for (int j = 0; j < pos.Length; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == pos.Length - 1) || (i == pos.Length - 1 && j == 0)) {
                    continue;
                }
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) {
                        Set(m, f, pos[i] + dx, pos[j] + dy, Math.Max(Math.Abs(dx), Math.Abs(dy)) != 1);
                    }
                }
            }
        }
        DrawFormat(m, f, 0);                                  // reserves the area, real bits come later
    }

    static int[] Align(int v)
    {
        if (v == 1) {
            return Array.Empty<int>();
        }
        int na = v / 7 + 2, size = v * 4 + 17;
        int step = (int)Math.Ceiling((v * 4 + 4) / (double)(na * 2 - 2)) * 2;
        int[] r = new int[na];
        r[0] = 6;
        for (int i = na - 1, p = size - 7; i >= 1; i--, p -= step) {
            r[i] = p;
        }
        return r;
    }

    static void DrawFormat(bool[,] m, bool[,] f, int mask)
    {
        int size = m.GetLength(0);
        int data = FormatBitsM << 3 | mask, rem = data;
        for (int i = 0; i < 10; i++) {
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        }
        int bits = (data << 10 | rem) ^ 0x5412;
        bool B(int i) => ((bits >> i) & 1) != 0;
        for (int i = 0; i <= 5; i++) {
            Set(m, f, 8, i, B(i));
        }
        Set(m, f, 8, 7, B(6));
        Set(m, f, 8, 8, B(7));
        Set(m, f, 7, 8, B(8));
        for (int i = 9; i < 15; i++) {
            Set(m, f, 14 - i, 8, B(i));
        }
        for (int i = 0; i < 8; i++) {
            Set(m, f, size - 1 - i, 8, B(i));
        }
        for (int i = 8; i < 15; i++) {
            Set(m, f, 8, size - 15 + i, B(i));
        }
        Set(m, f, 8, size - 8, true);                         // the dark module
    }

    static void DrawCodewords(bool[,] m, bool[,] f, byte[] data)
    {
        int size = m.GetLength(0), i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) {
                right = 5;
            }
            for (int vert = 0; vert < size; vert++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool up = ((right + 1) & 2) == 0;
                    int y = up ? size - 1 - vert : vert;
                    if (!f[y, x] && i < data.Length * 8) {
                        m[y, x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        i++;
                    }
                }
            }
        }
    }

    static void ApplyMask(bool[,] m, bool[,] f, int k)
    {
        int size = m.GetLength(0);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (f[y, x]) {
                    continue;
                }
                bool inv = k switch {
                    0 => (x + y) % 2 == 0,
                    1 => y % 2 == 0,
                    2 => x % 3 == 0,
                    3 => (x + y) % 3 == 0,
                    4 => (x / 3 + y / 2) % 2 == 0,
                    5 => x * y % 2 + x * y % 3 == 0,
                    6 => (x * y % 2 + x * y % 3) % 2 == 0,
                    _ => ((x + y) % 2 + x * y % 3) % 2 == 0,
                };
                if (inv) {
                    m[y, x] = !m[y, x];
                }
            }
        }
    }

    // the four penalty rules of the standard
    static int Penalty(bool[,] m)
    {
        int size = m.GetLength(0), p = 0, dark = 0;
        for (int pass = 0; pass < 2; pass++) {
            for (int a = 0; a < size; a++) {
                int run = 1;
                for (int b = 1; b <= size; b++) {
                    bool same = b < size && Get(m, pass, a, b) == Get(m, pass, a, b - 1);
                    if (same) {
                        run++;
                        continue;
                    }
                    if (run >= 5) {
                        p += 3 + run - 5;
                    }
                    run = 1;
                }
                for (int b = 0; b + 11 <= size; b++) {         // 1:1:3:1:1 with 4 light on one side
                    string s = "";
                    for (int k = 0; k < 11; k++) {
                        s += Get(m, pass, a, b + k) ? '1' : '0';
                    }
                    if (s == "10111010000" || s == "00001011101") {
                        p += 40;
                    }
                }
            }
        }
        for (int y = 0; y + 1 < size; y++) {
            for (int x = 0; x + 1 < size; x++) {
                if (m[y, x] == m[y, x + 1] && m[y, x] == m[y + 1, x] && m[y, x] == m[y + 1, x + 1]) {
                    p += 3;
                }
            }
        }
        foreach (bool d in m) {
            if (d) {
                dark++;
            }
        }
        int total = size * size;
        int kdev = (Math.Abs(dark * 20 - total * 10) + total - 1) / total - 1;
        return p + Math.Max(0, kdev) * 10;
    }

    static bool Get(bool[,] m, int pass, int a, int b) => pass == 0 ? m[a, b] : m[b, a];
}
```

