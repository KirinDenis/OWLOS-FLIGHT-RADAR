# The history window

OWLOS Flight Radar's site shows how the project grew, at the bottom of its page, in one window with three views
and a menu. Built 2026-11-24 with the maintainer, turn by turn; the code is `files/site/` (copied from the radar's
`site/` on main, 2026-11-25). This file says what it is, why each part looks as it does, and how it was tested.

## What it is

One window of the site's own window system (`win.js`, below), docked in a `<section id="history">` at the bottom of
the page. Inside, a grid: the picture on the left with three tabs over it, the menu on the right.

- **TOWER** (`tower.js`, WebGL with three.js r170, loaded only when the tab is first opened): the history as a tower
  of time. The floor is the radar's own dial cut into the product's parts (history-from-git.md); the height is time,
  a commit a step up (3.4 units). A file is a thread rising from the commit that brought it to the one that took it
  away, thicker the more of its lines were ever changed. A commit is a bead on the axis with curved light to each
  file it touched, gathered through its part's gate, so what changed together shows. A version is a glass floor
  with its number. Hovering a bead says the commit's own words and its lines added and taken out per part.
- **CODE** (`growth.js`, canvas 2D): the files growing commit by commit in the manner of Gource. A folder is a
  branch held to its parent and apart from the others by springs; its files sit round it in a sunflower (each file
  at the golden angle, 2.39996 rad, from the last); a file's colour is its kind; whoever wrote a commit flies to the
  files it touched and lights them. Bloom by a blurred copy (`ctx.filter = 'blur(5px)'`) added over the picture.
- **BOARDS** (`history.js`, SVG): the versions on the radar's dial. A ring a version, the first in the middle and the
  newest at the edge; a board a line at a bearing of its own, from the version that brought it to the last one it
  is in, coloured by its chip and nothing else.
- **The menu** (`#histpanel.histmenu`, right of the picture): the version shown, its summary line and its points,
  the version buttons, the PARTS chips (ALL and one a part), and SHOW ALL AGAIN once anything was picked or moved.
  On a phone the menu goes under the picture; in a maximized window on a phone it is hidden.

The window starts playing when it is first scrolled into view (an IntersectionObserver), a version at a time, and
stops on the newest. Nothing moves when nothing plays: the loop sleeps until something wakes it [built, measured in
the pane].

## The road the design took

Each turn below is the maintainer's reaction, in their words translated, and what it changed. The lessons are the
point: they hold for any picture of a project.

1. **The first try was a radial dial of versions**, because the radar is radial. Liked: "very informative, the
   palette is good". But: "more smoothing, animation, halftones, blur, it's a radar; when you hover or click the
   numbers there is no animation, no physics, linear code". So the dial got a spring (zeta 0.72) to a picked
   version, a glow filter, half-tone gradients, a sweep with line flares. *Lesson: a still diagram of a living
   thing reads as dead; motion with physics is part of the information.*
2. **"I thought of something like Gource, analysing the history and drawing it."** The CODE view was added beside
   the dial (the maintainer picked "Gource plus the dial" over Gource alone). Seen running: "exactly what was
   needed, now the project itself shows how it grows."
3. **"Gource was interesting in its time, but today it can be done more interesting and clearer: graphs, maybe 3D,
   OpenGL in browsers is no problem."** The TOWER: the same history with time as height, so the whole life of the
   project stands at once and each version is a floor.
4. **"By the end it is very much and very bright, nothing can be told apart, just white dots."** Two causes: all
   light was added (additive blending), and every thread was lit at once. Fixed by:
   - plain blending for threads and beads, so a dense place does not burn white;
   - **one version lit at a time**: while playing, the version being written; after it, the one picked. The rest
     of the tower stands dim as context;
   - the threads cut at version starts, so only that version's stretch of a file lights;
   - a part's name on the floor isolates that part, the others hidden;
   - the bloom only for what is bright enough (a threshold), so the dim tower stays sharp.
   *Lesson: light that adds saturates where it is dense. Light the one thing being looked at and dim the rest.*
5. **"Now too dim"**, and **"picked one, can't get back"**. The dim level went up until the context read: LIT 1,
   DIM 0.36, bloom threshold 0.38. Every pick can be let go: the same version picked again goes back to all, a part
   picked again goes back to ALL, and SHOW ALL AGAIN appears whenever anything is picked, zoomed or moved. A double
   click on the picture does the same. *Lesson: tune brightness between two complaints, and never make a state
   without a visible way back.*
6. **"Full screen, I meant our window system: maximize its windows. And you made two windows, it is one; where the
   version is chosen is just a menu inside this window, on the right."** A FULL SCREEN button with the browser's
   requestFullscreen had been added; it went. The history became one `Win` of the site's own desk, with the menu
   inside it, maximized by the window's own button like every other window on the site. *Lesson: use the product's
   own interface for a new thing before inventing a control; read "full screen" in the product's words.*

## The window system (win.js)

The site's windows: dragged by the title, on top when touched, minimize, maximize and close. They lie as tiles under
the picture, and on a phone they follow each other as a page does [built, used by every window of the site].

```js
const win = desk.make('history', 'How it grew, version by version', {
  className: 'history',
  dock: document.getElementById('history'),   // a place of its own on the page instead of a tile; not dragged
  onResize: () => { /* the views measure themselves again */ },
  onClose: () => { /* show the button that opens it again */ },
  onFront: () => {},
});
win.body.append(document.querySelector('.historygrid'));
win.show();
```

- `dock` was added for this window: a docked window lives in its section, is not dragged, and is maximized like the
  others.
- Maximized, it is fixed over the page with a 12 px margin (6 px on a phone); its body is a flex box, the picture
  takes what the menu leaves.
- The picture's height in a maximized window is measured, not guessed: the body's bottom minus 14, minus the
  picture's top, the tabs, the hint line and 22 px. A first guess made the canvas taller than the window [measured].
- Closing it leaves a small button in the section to open it again.

## Controls

The same in TOWER and CODE, so one habit works for both:

- drag turns the tower (or pans the code); shift or the right button lifts the tower up and down;
- the wheel comes closer only in a maximized window or with ctrl, so the page still scrolls under the mouse;
- two fingers pinch closer; one finger on the page only turns, so the page still scrolls;
- double click: everything back, as SHOW ALL AGAIN;
- the CODE view zooms to the cursor, not to the middle.

A hint line under the picture says them in words, a different line when maximized.

## Speed and the frame loop

- The frame time is clamped: `dt = max(0, min(0.05, (ms - last) / 1000 || 0.016))`. Without the `max(0, ...)` a frame
  stamped before the loop woke gave a negative dt, and the tower's springs went NaN; found while filming with time
  stepped by hand (capture-in-a-hidden-pane.md) [measured], fixed in all three views.
- Threads, beads and hubs are three `InstancedMesh`es, one draw call each; the curves are `CatmullRomCurve3` through
  the part's gate [built].
- A browser without WebGL gets `null` from `startTower`, and the page keeps its other two views.
- A view that was hidden when the window opened measures itself again when its tab is shown (`setActive` calls
  `refit()`); the CODE view had stayed fitted to a zero size [measured].

## How it was tested

- A test copy of the site with the files beside it (CHANGES.md, fw/versions.json, history.txt) on a local server,
  driven by the agent in its browser pane (esp32-flash-and-settings: fake-board-testing.md).
- The pane is often hidden from the agent: its requestAnimationFrame runs about twice a second and CSS animations
  stand still, so a screenshot shows a frozen or half-drawn state [measured]. For checks, step time by hand; for
  pictures, save frames through a local sink (capture-in-a-hidden-pane.md).
- The maintainer saw it on the local server at each turn; nothing went to the live site until they committed.

## Carrying it to another project

1. Add `history.sh` to the site's workflow (history-from-git.md), with `fetch-depth: 0`.
2. Put a summary line under each heading of CHANGES.md.
3. Copy `history.js`, `growth.js`, `tower.js` and three.js r170's `three.module.js` (MIT, credit it in the footer).
   Change PARTS in `tower.js` to your product's parts and KINDS in `growth.js` to your file kinds; check the colours
   for colour-blind eyes.
4. If the site has no window system, take `win.js` or dock the grid in a plain section; the views only need a box
   to draw in.
5. The BOARDS dial is the radar's own (its boards and chips from fw/versions.json); another project draws its own
   third view from what it ships, or keeps two.
