# Animation

What moves, how it moves without depending on the frame rate, and what must stay still. Code: Air Quality's
spring and slide in [code-hud.md](code-hud.md), WiFi Radar's easing and the 360 turn in
[code-scope.md](code-scope.md). The rule behind it, in short: the instrument may move, a value arriving may move,
the weather may move; decoration on a screen people read does not (data-screens.md).

How sure: (M) seen on a real board, (P) built and seen in the preview or read in the code, (R) reasoned.

## Time, not frames

Frames here are 50 to 125 ms and they change with the page. So what moves is a function of the clock:

```c
u->beam = wrap360((float)(now % 6000u) * 0.06f);                    // sweep: pure time, no state
float rot = (float)(u->now % 3000u) * (360.0f / 3000.0f);           // target arcs
float f = (float)age / 1600.0f;                                     // a one-shot: 0..1 from its start time
gfx_ring(cv, x, y, 5.0f + 16.0f * f, 1.0f, col, (int)(200 * (1.0f - f)));
```

Compare clocks with a signed difference, it survives the wrap: `(int32_t)(u->toast_until - u->now) > 0`.

The weather has its own clock: seconds since the dial or page came up (`W.anim`). The rain falls by it, the snow
drifts by it, the sun's rays turn by it. A picture given `t = 0` stands still: past hours' pictures on a graph do
(P).

## Smoothing, the right way

Chasing a target with a low-pass:

```c
x += (goal - x) * clampf(dt * k, 0.0f, 1.0f);      // Air Quality: k 12 the tab marker, k 0.8 the night
```

WiFi Radar's `an->t += (ft - an->t) * 0.18f` is per frame, so it runs at half speed on an 8 fps board (R), and the
preview must tick at about 25 fps between shots or the shots differ. It stayed because it looks fine, but do not
copy that form. Angles go the short way: `ang += wrap180(goal - ang) * k`, or a blip crosses the whole disc going
from 359 to 1. On e-paper the factor is 1: a frame is seconds, no easing.

Keep `dt` sane first: `if (dt <= 0.0f || dt > 0.2f) dt = 0.05f;`

## Easing

```c
static inline float ease_out(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }   // arrivals
static float ease(float f) { return f * f * (3.0f - 2.0f * f); }                                          // the film's finger
```

`ease_out` for what arrives: a page sliding in (280 ms), the power-on intro (1.6 s) where the bezels draw themselves
in and ticks come one by one. A graph draws itself in from the left over 0.9 s when its page comes, once
(`PG_REVEAL_MS`, P).

## Springs, in substeps

The numbers on Air Quality's HUDs chase the readings with a spring just under critical damping, a tiny overshoot,
alive:

```c
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
```

`k = 40` for values, 30 for the pressure change. First value: no spring, set it. (R) With `k = 40`, `c = 10.75`, a
200 ms step has `c * h = 2.15`, and above 2 the velocity flips sign and grows each step. Any step under about 150
ms holds, 15 ms leaves a wide margin at almost no cost. The spring is on what is SHOWN: the alarm logic reads the
raw value.

Trap, springs that explode (M): numbers on the screen jumped around while the readings were fine. The frame's own
dt (200 ms in a slide with two pages drawn) in a plain step. Substeps of at most 15 ms, and clamp `dt`.

## Pulse and fade

- Alarm word: `a = 150 + 105 * (0.5 + 0.5 * sin(now * 0.0063))`, one pulse a second. A pulse, because a blink would
  hide the word half of the time.
- Emergency plane: colour alternates alarm and focus every 400 ms. Never dark.
- Bad air: halo `70 * (0.5 + 0.5 sin(4 t))`. Pressure alarm: 2.5 or 5 rad/s by how bad.
- HUD head breathing: `0.75 + 0.25 sin(2.2 t)`.
- Bezel glint 36 degrees a second (10 s a turn), dashed ring -7 degrees a second.
- Thermostat, relay on: dots of light flowing along the arc at 12 degrees a second.
- Caret 500 ms. Cross pulse once a second. Boot ring 1.8 s. Toast 2.2 s (1.8 to 2.4 by project, 5 s for a new
  weather warning).
- `LISTENING` and `WALK OR TURN 360` blink slowly: an instruction may, a value may not.
- A warning box breathes round its number (its fill and edge alpha follow a slow sine). The now dot on a graph
  pulses.

Trap, a blink hides the word (P): an alarm pulses alpha between 150 and 255, an emergency plane alternates between
two colours, never dark.

## The weather moves, the screen does not

On Flight Radar's WEATHER look (P in the preview, M on the boards):

- Rain falls from the clouds through the ring, slanted by the wind, each drop fading in and out at its own phase;
  on the graphs it falls from the line into the hour's bar. Snow drifts. Drops fall from the cloud in the small
  pictures and fade.
- The sun's rays turn slowly (a turn in some 45 s). Fog banks drift a little.
- Stars twinkle in the night part of the ring, each at its own pace, a few brighter ones as discs.
- Wind streaks drift through the glass the way the wind blows now, as fast as it blows (3 + 0.6 px/s per km/h),
  surging when the gusts are much stronger, each living 2.6 s and coming back elsewhere.
- Clouds sway a degree or two and breathe in and out of the ring's middle.
- The moon is drawn in its real phase.

What went: the radar's beam over the dial with its afterglow, the 6 s first turn that revealed the day, the sweep
over the graphs, the bars' falling highlights. The dial shows its day whole from the first frame. Without them the
CYD's dial went from 18 or 19 fps to 20, the round's disc from 67 to 90 ms to 51 to 55 ms (M).

## What is animated, and what must stay calm

Animated: the beam (on the radar, it is the instrument), a new thing arriving (a ring opens, once), the picked thing,
a value arriving at a new reading, a page change, an alarm, the weather itself. The instrument may breathe slowly.

Calm: values that have not changed, unknown things, anything the user is reading. In particular:

- **A hollow ring does not orbit.** Unknown direction used to circle slowly, and a circling dot looks like movement
  that is not there. Now it sits at a fixed angle from a hash of its address:

  ```c
  static float ghost_angle(const uint8_t *m)
  {
      uint32_t h = 2166136261u;
      for (int i = 0; i < 6; i++) h = (h ^ m[i]) * 16777619u;
      return (float)(h % 3600u) * 0.1f;
  }
  ```
- **The scale does not slide under the finger**: the thermostat's scale stays put while the target moves in its
  middle 60 %.
- **dB scales change in 10 dB steps**, or they would jump with every frame.
- **Planes move between answers** by dead reckoning along their track, smooth at any fps, but no more than 20 s on
  a guess (60 s on FR24's slower answers). Each correction glides over 2 s with a smoothstep; over 3.2 km it snaps.
  A plane in a turn follows an arc of at most 5 s and 90 degrees. Its nose glides too. The answer's own server time
  sets the position's time, not the moment the request started: timing from the request start made every answer up
  to 126 m off along the track and planes surged and crawled backwards (M, measured on recorded real answers: worst
  frame 600 m to 17 m, back-steps 6.6 to 0.6 px a plane-minute). Measure what the eye sees (nose, sideways, steps
  back) on recorded real data, not only jumps on made-up paths.
- **A printed screen keys off a whole quarter hour.** The paper dial's hand, loop and marks all use now rounded to 15
  minutes and the page is printed each quarter. A continuous now made the loop's points shift and sent 120 to 180
  changed pixels every 5 s (M, 4.2").
- On e-paper nothing moves at all.

Trap, a dot that circles (P): a network with no known direction slowly orbited its ring. It looked like movement
that is not there. Doubt sits still.

## The 360 turn's lobe (WiFi Radar)

WiFi Radar finds direction by the user's body shading the signal while they turn on the spot.

- Countdown 3 s: a big digit in a disc with a ring opening each second, the steps in the panel.
- Run 24 s, 15 degrees a second: the beam IS the heading to follow, slower and heavier (trail 40 degrees, a bright
  edge, a triangle on the rim, an arc filling round the rim, a percentage and seconds left). Swept area stays faintly
  lit.
- While it runs the **lobe** grows: the target's level per angle bin, a 1-2-1 average over 3 bins, drawn as a closed
  curve from the centre, `r = r0 + rs * (m - lo) / (hi - lo + 2)`. The `+ 2` keeps flat data from blowing up to
  full size. The curve is the measurement itself, drawn while it is measured, and it points at the network.
- Result 3.5 s: a ring opens from the centre, `5/12 FOUND`, `UP = WHERE YOU FACED`.

## Page slides and the intro (Air Quality)

Pages slide sideways in 280 ms (`SLIDE_MS`), both pages drawn in the strip during the slide; the tab marker slides
from tab to tab. The power-on intro takes 1.6 s. Code in code-hud.md.
