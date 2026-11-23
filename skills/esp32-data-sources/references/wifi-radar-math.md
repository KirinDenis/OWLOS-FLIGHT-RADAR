# WiFi Radar maths, in full

From OWLOS-WIFI-RADAR: `core/map.c` (the solver), `core/app.c` (levels, the turn), `core/ui.c` (`direction`), and
`tools/preview/walk.c` (the simulated flat). Whole in `code/wifi_map.c`, `code/wifi_app.c` and their headers. All of it is
portable C, built by ESP-IDF and by MSVC for the PC preview. Tags as in SKILL.md.

How sure: the formulas and constants are the code, read here. The accuracy numbers are from the simulation, [PC test].
Solve times are [measured] on the ESP32 and the C3. Nobody has measured the map against a tape measure in a real flat.

## 0. What it is, in honest words

- It is NOT triangulation. Triangulation needs angles, and RSSI gives no angle.
- It is trilateration while moving: range-only, the simplest form of WiFi SLAM. Each second gives a rough distance to
  each router from an unknown place. Walking gives many such places, and the fact that a person moves about 1 m a second
  ties them together. Walk and routers are solved jointly.
- The board has NO accelerometer and NO compass. Nothing measures a step or a heading. The walk is an unknown too.
- In the 360 turn the heading is just TIME: 15 degrees a second, if the person turns steadily. So it can only be as good
  as the person's steady turn.
- A walk alone cannot tell left from right. A mirror image of the map fits the levels exactly as well. The rotation is
  unknown too, it is pinned by convention.
- Indoors a bearing points where the signal ARRIVES from, through walls and doors. That is still the way to walk.
- The screen says so: `L/R ?`, `UP = YOUR FIRST STEPS`, a tilde on metres, a hollow ring for a network with no direction.
  A network with no known direction stays on its range ring at a fixed place. It must not orbit.

## 1. The model

Log-distance path loss, the level a router's frame arrives with:

```
rssi_hat = A - 10 n log10(d)                n = 2.8 (a flat with walls)
         = A - (KDB / 2) ln(q)              KDB = 10 n / ln 10 = 12.16  (dB per ln metre)
q        = dx^2 + dy^2 + D0SQ               D0SQ = 0.5 = (0.7 m)^2: nobody stands inside the router
```

`A` is the level at 1 m, its own unknown per router, typically A0 = -38 dBm.

The constants, all at the head of `map.c`:

```c
#define KDB      12.16f     // 10 n / ln 10 with n = 2.8, dB per ln(metre)
#define D0SQ     0.5f       // (0.7 m)^2, nobody stands inside the router
#define A0       (-38.0f)   // typical router level at 1 m
#define LAM_A    0.25f      // how hard A is pulled to A0: (4 dB / 8 dB)^2
#define SIG2     16.0f      // (4 dB)^2, noise of one second's level
#define VWALK2   1.0f       // (1 m/s)^2, walking speed
#define HUBER    6.0f       // past 6 dB a miss counts linear (doors, fridges...)
#define MOTION_MIN 0.35f    // see motion_evidence, picked from walk vs still simulations
```

The quick figure for the list, standing still (`app_distance_m`), only the order of magnitude, shown with a tilde:

```c
float app_distance_m(float rssi) { return powf(10.0f, (-38.0f - rssi) / 28.0f); }
```

## 2. The data: snapshots

- MAP_N 16 networks, MAP_T 160 snapshots. `int8_t m[T][N]`, dBm, HIST_NONE (-128) = not heard that second.
- `map_snapshot`, once a second, after the history rolled. The value is that second's mean, or the smoothed level if
  heard in the last 5 s (`roll_history`).
- A free slot takes the loudest new network at -88 dBm or better. Its first guess: at the distance its level gives from
  the last path point, in a random direction, `d = exp((A0 - rssi) / KDB)`.
- Snapshots stop while turning: the body changes the levels there, not the place. `pending` keeps counting, so the next
  snapshot's `dt` covers the gap, and the path may jump that far.
- Slots are keyed by BSSID, not the radio's ids: ids are new after each power on and a saved map has to find its
  networks again. `m->id[k]` is refreshed each snapshot, this session only.

Decimation, when the buffer is full: every two neighbours merge into one. The walk keeps its shape, only the time
resolution halves.

```c
static void decimate(map_t *m)
{
    int h = m->T / 2;
    for (int j = 0; j < h; j++) {
        int a = 2 * j, b = a + 1;
        for (int k = 0; k < m->n; k++) {
            int va = m->m[a][k], vb = m->m[b][k];
            m->m[j][k] = (int8_t)(va == HIST_NONE ? vb : vb == HIST_NONE ? va : (va + vb) / 2);
        }
        m->dt[j] = j ? m->dt[a] + m->dt[b] : m->dt[a];
        m->px[j] = 0.5f * (m->px[a] + m->px[b]);
        m->py[j] = 0.5f * (m->py[a] + m->py[b]);
        m->qx[j] = 0.5f * (m->qx[a] + m->qx[b]);
        m->qy[j] = 0.5f * (m->qy[a] + m->qy[b]);
    }
    m->turn_t /= 2;
    m->T = h;
}
```

## 3. The cost

Unknowns: path `p_t = (px, py)` for each second, and `(ax, ay, A)` for each router. With the miss
`e_tk = A_k - (KDB/2) ln(q_tk) - v_tk`:

```
C = sum_t sum_k  huber(e_tk)
  + sum_t  (SIG2 / (VWALK2 dt_t)) |p_t - p_(t-1)|^2         smoothness at 1 m/s
  + sum_k  LAM_A (A_k - A0)^2                               prior on A

huber(e) = e^2                 for |e| <= 6
         = 2 * 6 |e| - 36      past that
```

Why these weights. Everything is in dB squared, the units of the levels. Dividing the whole by SIG2 gives the plain
statistics: `e^2 / sigma^2` with sigma 4 dB, `|dp|^2 / (v^2 dt)`, a walk of 1 m/s as a random step, and
`(A - A0)^2 / (8 dB)^2`, since `LAM_A = 16 / 64`. The smoothness term is what makes the problem solvable at all.

Huber: a door, a wall or a fridge makes a level miss the model by far more than 4 dB. A square law would drag the path
for those. Past 6 dB a miss counts linearly. In the Gauss-Newton steps this is the weight `w = 1` inside, `w = 6 / |e|`
outside.

## 4. The path step: block-tridiagonal Gauss-Newton with Thomas

With routers held still, the second `t` sees the routers (a 2 x 2 block) and its neighbours through smoothness, so the
whole walk is one block-tridiagonal system, solved in O(T).

Per second, from the jacobian `J = de/dp = -KDB (dx, dy) / q`:

```
H_t = sum_k w J J^T          g_t = sum_k w J e
l_p = SIG2 / (VWALK2 dt_t)   (0 at t = 0)
l_n = SIG2 / (VWALK2 dt_(t+1))   (0 at the last)

(H_t + mu I + (l_p + l_n) I) d_t - l_p d_(t-1) - l_n d_(t+1) = b_t
b_t = -g_t - l_p (p_t - p_(t-1)) + l_n (p_(t+1) - p_t)
```

`mu` is Levenberg damping: 1.0 in a fresh start, 0.5 otherwise.

Forward sweep, then back substitution, `d_t = s_d[t] - s_C[t] d_(t+1)`:

```c
static float s_C[MAP_T][4], s_d[MAP_T][2];

        float M00 = h00 + mu + lp + ln, M01 = h01, M10 = h01, M11 = h11 + mu + lp + ln;
        if (t > 0) {
            M00 += lp * s_C[t - 1][0]; M01 += lp * s_C[t - 1][1];
            M10 += lp * s_C[t - 1][2]; M11 += lp * s_C[t - 1][3];
            b0 += lp * s_d[t - 1][0];
            b1 += lp * s_d[t - 1][1];
        }
        float det = M00 * M11 - M01 * M10;
        if (fabsf(det) < 1e-9f) det = 1e-9f;
        float i00 = M11 / det, i01 = -M01 / det, i10 = -M10 / det, i11 = M00 / det;
        s_C[t][0] = -ln * i00; s_C[t][1] = -ln * i01;
        s_C[t][2] = -ln * i10; s_C[t][3] = -ln * i11;
        s_d[t][0] = i00 * b0 + i01 * b1;
        s_d[t][1] = i10 * b0 + i11 * b1;
    }
    float x0 = 0, x1 = 0;
    for (int t = T - 1; t >= 0; t--) {
        float n0 = s_d[t][0], n1 = s_d[t][1];
        if (t < T - 1) {
            n0 -= s_C[t][0] * x0 + s_C[t][1] * x1;
            n1 -= s_C[t][2] * x0 + s_C[t][3] * x1;
        }
        x0 = n0; x1 = n1;
        float l = sqrtf(x0 * x0 + x1 * x1), s = l > 3.0f ? 3.0f / l : 1.0f;   // max 3 m per step
        m->px[t] += x0 * s;
        m->py[t] += x1 * s;
    }
```

The check that the recursion is right: put `d_(t-1) = s_d[t-1] - s_C[t-1] d_t` into row `t`. The term `-l_p d_(t-1)` moves
`l_p s_C[t-1]` into the matrix and `l_p s_d[t-1]` into the right side, which is what the code adds.

A step is capped at 3 m a second. The two static arrays are the only scratch.

## 5. The router step: 3 x 3

With the walk held still, each router is on its own: position and level at 1 m, against all the seconds that heard it.

```
J = de/d(ax, ay, A) = ( KDB dx / q, KDB dy / q, 1 )
H = sum_t w J J^T,  g = sum_t w J e
H[2][2] += LAM_A;   g[2] += LAM_A (A - A0)         the prior on A
```

Needs 6 seconds heard, or `sd = 99` and no step. Then `H + mu I` is solved by Gauss-Jordan with pivoting (`solve3`), the
position step is capped at 3 m and the `A` step at 6 dB. The sign is minus: `ax -= st[0]`.

## 6. Doubt, from the same normal equations

Before `mu` is added, the same `H` gives the doubt in metres:

```c
/* doubt: noise = this AP's own misfit, not the hoped 4 dB. x4 because neighbour
 * seconds share errors (walls dont move in a second). plus model error, a straight
 * line through a building: 0.5 m and 30% of the distance from the walk centre */
float var = ss / (float)(seen > 3 ? seen - 3 : 1);     // ss = sum w e^2
if (var < 4.0f) var = 4.0f;
// cov = 4 * var * H^-1, one column at a time through solve3
float a = cov[0][0], b = cov[0][1], d = cov[1][1];
float big = 0.5f * (a + d) + sqrtf(0.25f * (a - d) * (a - d) + b * b);   // the larger eigenvalue
cx /= (float)m->T; cy /= (float)m->T;
float reach = hypotf(m->ax[k] - cx, m->ay[k] - cy) * 0.3f;
m->sd[k] = sqrtf((big > 0 ? big : 0) + 0.25f + reach * reach);
```

So `sd = sqrt(largest eigenvalue + 0.5^2 + (0.3 distance)^2)`. Doubt is drawn. A network is `ok` for a DIRECTION from where
you stand when `d >= 1 m` and `atan2(sd, d) <= 30 degrees`.

## 7. Rounds, restarts, recentre

One round: `path_step`, then `ap_step` for each router, then `recenter`, then `map_yield()`.

```c
// shifting everything fits the same, so keep the walk centred on 0,0
static void recenter(map_t *m) { ... subtract the mean of the path from the path and the routers ... }
```

`map_solve(m, 4)` runs once a second, 4 rounds at `mu = 0.5`. It waits for `T >= 12` and `n >= 3`.

The cost has lots of local minima. So at the first solve and every 20 s, fresh starts compete with the answer in hand,
the cheapest one wins:

```c
if (!m->have || (int)m->seconds >= m->next_restart) {
    float best = 1e30f;
    if (m->have) { run(m, 4, 0.5f); best = cost(m); save(m, &s_best); }
    for (int r = 0; r < map_restarts; r++) {
        randomize(m);
        run(m, 30, 1.0f);
        float c = cost(m);
        if (c < best) { best = c; save(m, &s_best); }
    }
    load(m, &s_best);
    m->have = 1;
    m->next_restart = (int)m->seconds + map_restart_every;
}
run(m, iters, 0.5f);
```

A fresh start: each router at the distance its mean level gives, clamped 1.5 to 25 m, random direction, `A = A0`; the
walk piled up within +-0.25 m of the centre.

Defaults: `map_restarts = 6`, `map_restart_every = 20`. That is about 180 rounds. [measured] 1.2 to 1.7 s of maths on the
ESP32 with 160 snapshots and 16 networks. Next to a busy router the WiFi took most of core 0 and the same work stretched
to 10 s: nothing of lower priority ran, the idle task starved and the task watchdog rebooted the board, again and again.
Hence `map_yield`, pointed at `vTaskDelay(1)`, once a round. On the C3 (one core, no FPU) the restarts took 6 to 10 s even
with 4 networks: there `map_restarts = 3`, `map_restart_every = 60`, and the mapper shares the screen's priority.

`mapper_task`: core 0, priority 2 (1 on the C3), stack 6144, once a second. The lock is held for the snapshot and the
publish, not for the solve: the solver works on its own copy.

## 8. Keeping the picture steady: weighted Procrustes

Each solve may turn, shift or even mirror the whole solution, since those fit the same. The screen must not jump. So
there are two frames: the solver's (`px, ax`) and the published one (`qx, bx`). Each second the rigid move that best lays
the new solution over the old picture is found and applied.

Weights: 1 for a path point, `4 / (1 + sd^2)` for a router, so a doubtful one counts for less. With both sets centred on
their weighted means:

```
Sxx = sum w sx tx,   Sxy = sum w sx ty,   Syx = sum w sy tx,   Syy = sum w sy ty
plain rotation:   a1 = Sxx + Syy,  b1 = Sxy - Syx
mirror x first:   a2 = Syy - Sxx,  b2 = -Sxy - Syx
mirror if a2^2 + b2^2 > a1^2 + b1^2
th = atan2(b, a)
x' = c x - s y + tx,  y' = s x + c y + ty      (x = -x first when mirrored)
```

The mirror is allowed so that a solver which came back flipped is put back the way the screen had it.

## 9. Which way is up

- Before anything: up = the way the walk first went. Once the path got 1.5 m from its start, the published picture is
  turned round the start by `-compass(dx, dy)`, once (`framed`).
- `compass(dx, dy) = atan2(dx, dy)` in degrees, 0 = up, clockwise.
- A good turn replaces this: up = where you faced when the turn began (`aligned`).
- Heading from the walk: from the last 6 s of published path. Standing still, the solved path still shakes by about 1 m
  and points anywhere. So it counts only if the straight line is at least 2 m, at least 60% of the path walked, and, once
  aligned, at least 2.5 m from the spot of the turn. Each new heading goes halfway from the old one.

```c
int T = m->T, k = T - 1;
float acc = 0, len = 0;
while (k > 0 && acc < 6.0f) {
    len += hypotf(m->qx[k] - m->qx[k - 1], m->qy[k] - m->qy[k - 1]);
    acc += m->dt[k--];
}
float hx = m->qx[T - 1] - m->qx[k], hy = m->qy[T - 1] - m->qy[k];
float disp = sqrtf(hx * hx + hy * hy);
int away = !m->aligned || hypotf(m->qx[T - 1] - m->qx[m->turn_t], m->qy[T - 1] - m->qy[m->turn_t]) >= 2.5f;
if (disp >= 2.0f && disp >= 0.6f * len && away) {
    float h = compass(hx, hy);
    if (m->heading_ok) {                          // smooth: go halfway to the new one
        float d = fmodf(h - m->heading + 540.0f, 360.0f) - 180.0f;
        h = fmodf(m->heading + 0.5f * d + 360.0f, 360.0f);
    }
    m->heading = h;
    m->heading_ok = 1;
}
```

## 10. Did we really walk? motion_evidence

A radar on a table still "walks" about 1 m in the solution: a free path fits the noise. So the solution is compared with
standing still, each router at its mean level all the time, with misses clamped to 6 dB:

```
still = sum clamp(v - mean)^2
moved = sum clamp(e)^2
motion = clamp01(1 - moved / still)
```

The map goes on the screen only when all of these hold:

```c
v->active = m->framed && spread >= 3.0f && T >= 20 && v->motion >= MOTION_MIN;   // MOTION_MIN 0.35
```

`spread` is how far the walk got from its middle, in metres. 0.35 was picked from simulations of walking against lying
still. [PC test]

## 11. The 360 turn

A body eats 2.4 GHz well, 5 to 15 dB. So with the board at the chest, each network is loudest when you face it.

- 3 s countdown (`SWEEP_COUNTDOWN_MS`), 24 s turn (`SWEEP_RUN_MS`, 15 degrees a second), 3.5 s result.
- The heading is time: `h = (now - t0) * 360 / 24000`. Nothing else.
- 24 slices of 15 degrees (`BEAR_BINS`). Each frame on the network's OWN channel goes into its slice. A slice is the sum
  and count kept in the contact, so the expiry of silent contacts is off during a turn.
- The radio visits only channels where something lives, 110 ms each, so each network is heard as often as possible.

The first harmonic fit, per network, weighted by frames a slice:

```
rssi(h) ~ a + p cos h + q sin h       least squares over the slices heard
phi     = atan2(q, p)                 the peak: the direction
b       = sqrt(p^2 + q^2)             swing = 2 b
```

Not enough to fit: under 5 slices or under 6 frames, or a gap of over 120 degrees with nothing heard (two rounds of the
circle for the wrap). A weak network is heard a few times in 24 s, so the circle need not be full.

The inverse of the 3 x 3 normal matrix is also the covariance, so the doubt in the angle comes by the delta method:

```
var  = max(ss / (total - 3), 9)         never trust one frame better than 3 dB
vphi = var (p^2 I22 + q^2 I11 - 2 p q I12) / b^4
sd   = sqrt(vphi)                       in degrees
```

Rejected: `b < 1.5` dB or `sd > 60`: no lobe worth trusting. `bearing_sd = max(sd, 15)`: even a steady hand in a clean room
leaves about 15 degrees of doubt, so it is floored there.

## 12. map_align_turn: the turn fixes left, right and up

The map says where each router is from here (`phi`, by the map), the turn says where it is by the body (`beta`). If the
map is right, `beta - phi` is the same angle for all of them, that angle is the rotation; mirrored, `beta + phi` is. So
both are tried, as a weighted mean of unit vectors:

```c
for each network with a slot and a bearing:
    dx, dy = router - where you stand;  d = length;   skip if d < 1 m
    map_sd = atan2(sd[k], d) in degrees;              skip if over 50
    w = 1 / (bearing_sd^2 + map_sd^2)
    for r in 0, 1:                                    // 0 as is, 1 mirrored
        C[r] += w cos(beta - (r ? -phi : phi));
        S[r] += w sin(beta - (r ? -phi : phi));
    W += w; used++
if (used < 3) give up                                 // returns -(used) - 10
R0 = |(C0, S0)| / W,  R1 = |(C1, S1)| / W,  r = R1 > R0
// must agree well AND clearly better than the mirrored way, else left/right stays unknown. no guessing
if (R < (used >= 5 ? 0.55 : 0.7) || R - other < 0.15) return R;
th = atan2(S[r], C[r]);
transform_published(m, px, py, r, th);                // round where you stand
m->aligned = 1;  m->heading = 0;  m->heading_ok = 1;  // a full turn ends facing where it started
```

## 13. What the radar draws: direction()

```c
int turn_fresh = a->bearing_ms && (!mv->active || !mv->turned || mv->since_turn < 2.5f);
```

A turn's bearings hold only while you still stand there: 2.5 m away they are stale.

Per network: the map's direction relative to the heading, doubt `atan2(sd, d) + 15` degrees with a heading, `+ 25` without;
the turn's bearing with its sd. If both and the map is not aligned, the turn alone. If both and aligned, inverse-variance
weights, summed as vectors so 359 and 1 work:

```c
float w1 = 1.0f / (msd * msd), w2 = 1.0f / (c->bearing_sd * c->bearing_sd);
float x = w1 * cosf(ma * D2R) + w2 * cosf(c->bearing * D2R);
float y = w1 * sinf(ma * D2R) + w2 * sinf(c->bearing * D2R);
*ang = wrap360(atan2f(y, x) / D2R);
*sd = 1.0f / sqrtf(w1 + w2);
```

Neither: no direction, a hollow ring on the range circle.

## 14. How good: the simulated flat

`tools/preview/walk.c` runs the real `core/` against a flat where the truth is known, and prints the errors. [PC test]

The world, made to differ from the solver's model on purpose:

- 10 x 7 m flat, inner walls with doors, 11 networks: two of your own, a printer, neighbours behind walls, one a floor up,
  one hidden.
- Path loss exponent 3.0 (the model has 2.8), every router its own level at 1 m (-35 to -40).
- Walls cost 4 dB inside, 6 dB outside, 8 dB the floor slab.
- Own body: `8 (1 - cos rel) / 2` dB, rel the angle between facing and the router.
- Shadowing smooth over the flat, 2.5 dB; fading 3 dB gaussian. Frames off the channel 14 dB a channel lower.
- Weak beacons get lost: dropped with chance growing as the level falls toward -95.
- Two laps through every room at 0.8 m/s, then a 360 turn. A `still` run lies on a table instead.

Scored after the best rigid fit, rotation, mirror and shift, never scale (getting the scale right is the model's job).

Results as the README gives them: the turn about 5 to 11 degrees of error, the path within about 1.2 m, directions to the
networks it trusts within 10 to 30 degrees. A real flat is worse, because of reflections. Not measured there.

## 15. Saved across power off

`core/store.c`, `firmware/main/flashstore.c`. Struct as is behind a header: magic, version, sizes, so state from another
build is refused, not misread. Partition "radar", 512 KB, a ring of 12 KB slots. Every 3 minutes, and 5 s after a turn or a
new walk. Data first, then the header, then CRC32 checked on load, the newest good slot wins.

After a restore:

- `pending = 30`: the new walk joins the old with a loose 30 s gap, so the solver can find where you stand now.
- `heading_ok = 0`, you could be facing anywhere. `turned = 0`, the last turn was made somewhere else. Left/right from an
  old turn stays.
- `next_restart = seconds + 10`: fresh starts soon.
- `id[]` cleared: `map_snapshot` fills them by BSSID.
- Restored networks are drawn faded, and go in about 30 s unless heard. `last_ms = now - 15000`.

## Traps

- The board reboots again and again next to a busy router: restarts stretched from 1.5 s to 10 s, IDLE starved, task watchdog.
  `map_yield`: `vTaskDelay(1)` every round. [measured]
- The C3's picture stands still for seconds: no FPU, restarts took 6 to 10 s. 3 restarts every 60 s, the mapper shares the
  screen's priority. [measured]
- A map on a board lying on a table: a free path fits noise, it "walks" about 1 m. `motion_evidence`, spread 3 m, T 20. [PC test]
- The heading spins while standing: the same shake. Need 2 m, 60% straight, 2.5 m from the turn. [PC test]
- The map jumps or flips each solve: rotation and mirror fit the same. A published frame, Procrustes with mirror. [PC test]
- A saved map loses its networks: ids are new at power on. Slots by BSSID. [built]
- Unknown networks orbit the screen: drawn by a moving guess. A fixed place on the range ring, hollow. [the project's rule]
- The screen hitches at each save: a flash write under the lock. A small copy under the lock, the write without it. [built]
