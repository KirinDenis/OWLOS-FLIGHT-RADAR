# Touch: when to read, and the alignment

Every touch chip's driver returns raw numbers and does not try to orient anything; one alignment turns them into
screen pixels and sorts out swaps and mirrors for every kind. Sources: OWLOS Flight Radar `core/ui.c`,
`firmware/main/main.c`, `store.c`; the OWLOS Air Quality UI's `core/ui.c` and `touch.c`; OWLOS WiFi Radar's
`core/ui.c`. Sureness words as in [SKILL.md](../SKILL.md). The chips: [xpt2046](touch/xpt2046.md),
[gt911](touch/gt911.md), [ft6336](touch/ft6336.md), [cst816](touch/cst816.md), [gsl3680](touch/gsl3680.md),
[axs15231b](touch/axs15231b.md).

How sure: the 5 cross alignment on the 2.8" CYD, the 3.5" CYD and the round C3 (10 px) [measured]; the 9 point
quadratic on the Air Quality board (worst cross 8 px) [measured]; on the capacitive S3 boards [built], on the
CrowPanel Advance 5.0 [measured, a tester's board].

## When to read

```c
typedef struct { uint8_t down; int16_t x, y, rx, ry; uint32_t t; } tsample_t;
static tsample_t s_ts[24];
static int       s_nts;
static uint32_t  s_ts_last;
static uint32_t  s_touch_us, s_send_us;   // reading the touch and handing strips over, for the 10 s log

/* the first moments after power on the glass is not read at all, and a press it says from the start counts only
 * once it has lifted: a panel's chip asked while the screen's bus and the touch were still settling now and then
 * said a press that was not there (on the 3.5" a tap on the look's button: the look one on after a restart) */
#define TOUCH_DEAF_MS 1500u
static int s_touch_lifted;                             // the glass read lifted once after them: presses count from now on

static void touch_sample(int force)
{
    uint32_t now = now_ms();
    if (now < TOUCH_DEAF_MS) return;
    if (s_nts >= (int)(sizeof s_ts / sizeof s_ts[0]) || (!force && now - s_ts_last < 15u)) return;
    int rx = 0, ry = 0, x = 0, y = 0;
    uint32_t t0 = now_us();
    int down = tp_read(&rx, &ry);
    s_touch_us += now_us() - t0;
    if (!s_touch_lifted) {
        if (down) down = 0;                            // pressed since before it was listened to: not yet a finger's
        else s_touch_lifted = 1;
    }
    if (down) touch_apply(&s_ui.cal, rx, ry, &x, &y);
    s_ts[s_nts++] = (tsample_t){ (uint8_t)down, (int16_t)x, (int16_t)y, (int16_t)rx, (int16_t)ry, now };
    s_ts_last = now;
}
```

- Called with `force` once at the top of the frame, and with 0 after every strip, so the 15 ms limit holds it to
  about 60 samples a second however long the frame is. The queue is read at the top of the next frame, each sample
  with its own time, so speed and hold times come out right at 8 fps too.
- Why between the strips: at 16 fps a frame is enough, at 8 fps a swipe gave one or two points and came out as a
  tap [measured].
- The deaf 1.5 s and the lift rule are new since the first version of these notes: phantom presses after a
  restart (with the XPT2046's two-reads rule, [xpt2046](touch/xpt2046.md)).
- On the boards with a moving layer a pass can outlast a frame, so a lift there needs four empty reads.

## The 5 cross alignment

`touch_cal_t` is an affine map from raw to screen:

    x = a * rx + b * ry + c
    y = d * rx + e * ry + f

Least squares over the crosses, on the normal equations: for each cross, f = (rx, ry, 1) adds f f^T to a
3x3 matrix, and f * X (or f * Y) to the right hand side. Solve both by Gauss with partial pivoting.
With 5 points and 3 unknowns a bad press shows as a large leftover: the worst cross over 18 px means
TRY AGAIN from the top.

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
```

Crosses and first guess, in `ui_init`:

```c
/* default touch alignment of the 2.8" CYD till the user aligns */
u->cal = (touch_cal_t){ 0.0f, -0.0894f, 333.3f, -0.0666f, 0.0f, 251.1f };
const int in = RADAR_ROUND ? (int)(GFX_W * 0.2f) : 30;   // round glass: the corners are not there, the diagonals are
const int pts[CAL_POINTS][2] = { { in, in }, { GFX_W - in, in }, { GFX_W - in, GFX_H - in },
                                 { in, GFX_H - in }, { GFX_W / 2, GFX_H / 2 } };
```

The page: a pulsing ring and a cross at the point, `PRESS AND HOLD THE CENTRE OF THE CROSS`,
`POINT n OF 5`, `BOOT BUTTON: SKIP`, a ring under the finger. Nail or stylus works best on resistive.

The rules in that code:

- `cal_wait`: the finger that opened the page has to lift first, or the first cross takes it.
- Samples only from 120 ms after the press, and the last 2 dropped: the landing and the lifting are
  where a resistive panel lies.
- A press under 300 ms counts as nothing, it is time not samples: at low fps a count of samples
  took over 0.5 s.
- `median16` of what is left.

How to open it: on first boot when no alignment is stored or the stored one fails the span check (below,
"Checking a stored alignment at boot"), from the setup page, or a finger held 4 s on nothing:

```c
if (!u->t_moved && u->t_long == 1 && u->t_hit == HIT_NONE && now - u->t_since >= 4000u) {
    u->t_long = 2;
    ui_start_calibration(u);                   // hold 4 s on nothing: align the touch again
}
```

A short BOOT on that page changes the screen profile on boards that have them, a long one skips
(`TOUCH SKIPPED`).

Where to store: an NVS blob `cal`, 24 bytes. Fall back to the sister firmwares' namespaces:

```c
int store_load_cal(touch_cal_t *cal)
{
    return load_blob(NS, "cal", cal, sizeof *cal) || load_blob("radarscan", "cal", cal, sizeof *cal) ||
           load_blob("thermo", "cal", cal, sizeof *cal);
}
```

## Checking a stored alignment at boot

Raw units across the screen differ by chip, so a stored map from another touch kind is off by ten times. At every
boot:

```c
int have = store_load_cal(&s_ui.cal);
const float want = (float)GFX_W / (float)tp_raw_span();
float sx = fabsf(s_ui.cal.a) + fabsf(s_ui.cal.b), sy = fabsf(s_ui.cal.d) + fabsf(s_ui.cal.e);
if (!have || sx < want / 3 || sx > want * 3 || sy < want / 3 || sy > want * 3) ui_start_calibration(&s_ui);
```

`TOUCH_RAW_SPAN` (board.h): XPT2046 3700, CST816 240, FT6336 320 (it counts screen pixels, 240x320 standing), GT911
`GFX_PW` (it counts the glass's pixels, lying down), the Tab5's and the JC8012's `GFX_PW` (the long way of the
standing glass is the picture's width). At run time (`tp_raw_span()`), because the 3.5" learns in `tp_init` whether
it has a GT911 (480) or an XPT2046.

## The screen flipped: the map turned, saved, no crosses

```c
/* the screen turned 180 degrees, from its setup page or from the site. the touch alignment
 * turns with it, so it needs no new crosses */
static void screen_flip(int on)
{
    disp_flip(on);
    touch_cal_t *c = &s_ui.cal;
    c->a = -c->a; c->b = -c->b; c->c = (float)(GFX_W - 1) - c->c;
    c->d = -c->d; c->e = -c->e; c->f = (float)(GFX_H - 1) - c->f;
    store_save_cal(c);
}
```

Flip without turning the alignment and taps land on the opposite side. After a screen profile change the
alignment is not redone: a profile that mirrors an axis leaves the old map wrong until the next alignment
[reasoned from the code, not seen].

## Boards that skip the crosses

The Waveshare 7/7B and the JC8012 builds: a capacitive chip that counts the glass's own pixels, so how its numbers
fall on the screen is known. The map is the glass scaled down to the layout, turned with the picture when it is
turned, and the 5 cross alignment is never started:

```c
s_ui.cal = (touch_cal_t){ (float)GFX_W / (float)GFX_PW, 0, 0, 0, (float)GFX_H / (float)GFX_PH, 0 };
if (c.flip) {                                      // as screen_flip turns it
    touch_cal_t *k = &s_ui.cal;
    k->a = -k->a; k->b = -k->b; k->c = (float)(GFX_W - 1) - k->c;
    k->d = -k->d; k->e = -k->e; k->f = (float)(GFX_H - 1) - k->f;
}
```

A port of those builds to a resistive panel wants the alignment back. All other touch boards, the scaled 5" and 7"
ones included, run the crosses once (`RADAR_TOUCH_CAL`): the resistive ones need it anyway, and on the round one it
finds out how the chip's axes sit. FusionEdge aligns only the XPT2046 (`LITE_CALIBRATE`); its capacitive boards take
the chip's pixels as they are [docs].

## Axes, by board

- 2.8" CYD at MADCTL 0xE8: swapped, both reversed ([xpt2046](touch/xpt2046.md) has the corner readings and the
  default map) [measured].
- Round board: the alignment finds out how the chip is turned, nothing is assumed [measured].
- FT6336 on a 240x320 panel standing counts the portrait glass's pixels, so in landscape expect a swap. The GT911
  likewise, portrait 320x480 on the 3.5" [built].
- The 3.5" and the Air Quality panel: same method, no fixed numbers kept.

## The 9 point quadratic, Air Quality

On the 3.5" RPi LCD the affine map left the bottom row 15 to 20 px off: a resistive panel bends towards
its edges. So a quadratic map, six terms per axis, 9 crosses (28 px in, three by three), worst cross 8 px
on the board [measured]. The threshold is 14 px; 9 points with 6 terms fit a good press within a few px.

```c
/* raw touch chip numbers -> screen pixels. u = raw x / 4096, v = raw y / 4096, terms 1 u v uv u^2 v^2 */
#define TOUCH_CAL_MAGIC 0x51434132u            /* "2ACQ": an older map in NVS is not this one */
typedef struct { uint32_t magic; float ax[6], ay[6]; } touch_cal_t;
```

The crosses, left to right, top to bottom:

```c
const int in = 28;
for (int i = 0; i < CAL_POINTS; i++) {
    CAL_PT[i][0] = i % 3 == 0 ? in : i % 3 == 1 ? GFX_W / 2 : GFX_W - in;
    CAL_PT[i][1] = i / 3 == 0 ? in : i / 3 == 1 ? GFX_H / 2 : GFX_H - in;
}
```

The fit and the checks:

```c
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

void ui_start_calibration(ui_t *u)
```

The fit itself is the same loop as the 5 crosses with `NT` 6 in place of 3 and `terms()` for `f`, then
`worst > 14.0f || !touch_cal_sane(&fit)` is TRY AGAIN. Dividing raw by 4096 keeps the float matrix
well scaled: u^2 terms at raw 4000 would be 1.6e7.

Starting guess until the crosses are done: `ax[1] = GFX_W`, `ay[2] = GFX_H`, the rest 0.

`touch_cal_sane` is the stronger form of the span check: the panel's four corners must map near the
screen's and span at least half of it, and NaN fails it.


## Sampling numbers

- XPT2046: `Z_MIN` 350, edge limits 50 and 4050, 5 reads per axis, median, a press from two reads in a row
  [measured on the 2.8", the 3.5", the Air Quality board].
- Pen check every 20th sample, so a dead pin costs at most 20 samples, about 0.3 s, once.
- GT911: a silence of 300 ms is a lift.
- Power on: 1.5 s deaf, then a press held from the start counts only after it lifts.
- The alignment page: 120 ms, 300 ms, 2 samples dropped, at most 48 samples.
- Buttons over a map answer further out than they are drawn (8 to 12 px): a finger is wide and a resistive panel
  is a few pixels off.
