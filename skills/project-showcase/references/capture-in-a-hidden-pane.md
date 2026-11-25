# Capturing a page frame by frame in a hidden pane

The agent's browser pane is often not on screen. A page there still runs, but not as on a screen, so pictures and
timing taken from it lie unless time is stepped by hand. Found and solved 2026-11-24 while filming the history
window for the 0.16 film; the code is `files/capture/frame-step.js` and `files/capture/sink.ps1`.

## What a hidden pane does [measured]

- `requestAnimationFrame` runs about twice a second, not sixty times.
- The CSS animations' timeline stands still: a transition started never ends, an element fading in stays invisible.
- A screenshot of the pane comes back small (about 800x600 for a 1024 px wide page) and scaled: no good for a film.
- A WebGL canvas read back with `drawImage` or `toBlob` is blank unless it was made with `preserveDrawingBuffer`.
- The page itself is fine. Its scripts, its network and its DOM all work.

## The recipe

1. **Start the sink**, a small local server that saves what is posted to it:
   `powershell -ExecutionPolicy Bypass -File sink.ps1 -Out <folder>` (port 8798). The page cannot write files; the
   sink can. A run in a terminal of the agent's own, stopped by `POST /quit`.
2. **Load the page, then paste `frame-step.js` at once**, before the animated part is first seen: it patches
   `getContext` so every WebGL context keeps its drawing buffer, and it replaces `requestAnimationFrame` with a queue.
3. **Step time by hand.** `frameStep.step(ms)` moves the clock and runs the queued frames once. The clock is
   `T = max(T + ms, performance.now())`: never behind real time. If it falls behind, a page that measures its own
   time from `performance.now()` sees time run backwards, and a page that counts its tweens from the rAF stamp gets a
   negative dt. That is how a real bug was found: the tower's dt went negative and its springs NaN'd (fixed in the
   product, dt is clamped at 0).
4. **Finish the CSS animations** before each grab (`document.getAnimations()` and `finish()`): the frozen timeline
   would otherwise keep a fading element invisible.
5. **Grab**: a canvas the element's size, each canvas of the element drawn where it lies, then the element's HTML
   cloned into an SVG `foreignObject` with the page's own CSS, canvases hidden in the clone, drawn over. Then
   `toBlob` and POST to the sink under a numbered name.
6. **Run the loop in the background** (`frameStep.film(...)` returns a promise and is not awaited). The agent's
   tool call times out after about 45 s; the loop goes on, and the files keep landing. Count them in the folder.
7. **Make the clip** with ffmpeg from the numbered PNGs at the frame rate the steps meant:
   `ffmpeg -framerate 10 -i h-%04d.png -c:v libx264 -crf 10 -pix_fmt yuv444p clip.mp4` (two steps of 50 ms a frame
   is 10 frames of page time a second).

About 0.4 s of real time a frame, for a 1700x900 window with a WebGL view [measured].

## Traps

- **What foreignObject cannot draw**: `backdrop-filter` (glass blur) and a few pseudo-elements. Turn them off in
  the clone's CSS, or the glass panes come out solid.
- **A fixed element** (a maximized window) is cloned with its `position: fixed` and offsets: in the clone set it
  static at 0,0 at its measured size.
- **Grab the element, not the page**, and at the size the page laid it out: scaling a page to fit made it blurry.
  Grabbing a window by its outer box once shifted the picture by its absolute left; grab its inner panels, or
  reset its position in the clone as above.
- **Press buttons and set inputs by code** (`click()`, `value` and an `input` event), then step a few frames so the
  page reacts before the first grab.
- **The pane may be emulating a size** set earlier: reset it, or the layout under test is not the real one.

## The same trick for tests

Stepping time is also how an agent tests an animation without watching it: step to a moment, read the DOM or the
canvas, compare. A check that waits for real time in a hidden pane waits thirty times too long and still sees a
frozen CSS state.
