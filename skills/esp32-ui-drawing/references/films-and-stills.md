# Films and stills

How the screen films and stills are made: the board's own screen code drawn on a PC, a finger drawn over it,
captions read by the page, and the way a longer film is put together. Code: TOUR.BAT, tour.c, GIF.BAT and the page's
caption code in [code-tools.md](code-tools.md). The preview it grows from: pc-preview-tool.md.

How sure: (M) made and watched, (P) built, (R) reasoned.

## The tour film (tools/preview/tour.c)

`TOUR.BAT` builds `tour.c`: the same screen code plays a script of touches over the demo sky, frames go to ffmpeg as raw
RGB through a pipe, out comes an MP4 of the screen alone.

```c
snprintf(line, sizeof line, "%s -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
         "-c:v libx264 -preset slow -crf 24 -tune animation -pix_fmt yuv420p -movflags +faststart \"%s\"",
         argv[1], OUT_W, OUT_H, FPS, argv[2]);
film = _popen(line, "wb");
```

- 25 fps, each screen pixel 2x2 (1x1 on scaled glass, it is big enough), 90 s of sky flown first so trails are grown.
- Verbs: `wait`, `glide` (follows a moving plane), `tap`, `press`, `drag_polar`, `drag_line`, `caption`. `ease()` moves
  the finger (a smoothstep).
- A **finger is drawn over the frame**, not in core: a soft light disc, firmer when pressed, a ripple when it lets go. A
  film without it shows things changing by magic.
- `caption(a, b)` stamps the time, and the end writes WebVTT. The site does not show captions over the picture: the track
  is read and the lines go under the board:

  ```js
  track.mode = 'hidden';
  track.oncuechange = () => { const c = track.activeCues && track.activeCues[0]; if (!c) return;
    const [a, b] = c.text.split('\n'); $('cue1').textContent = a; $('cue2').textContent = b || ''; };
  ```
- The video sits in a photo of the board: `<video class="board-screen">` over `<img class="board-photo">`.
- It writes a poster PNG too: the page shows a picture before the video plays.
- `GIF.BAT`: the film inside the photo, two parts of it, 8 fps, 128 colours, Bayer dither, 4.4 MB for the README.
- Built with `PROMO` (`TOUR.BAT promo`) it is the short film for posts instead: the 800x480 glass, a script of what a
  version added, its captions in a band of 80 rows under the screen in FONT_TITLE, a last card with the name in FONT_BIG
  (two film-only fonts baked from Cascadia Mono, ASCII only) (P).

## A video opens on a picture

The first frame of any video made for posts must be a full picture: no fade-in from black, no black frame at the start.
Fades are fine between slides and at the end. Networks take the first frame as the thumbnail, and a fade-in posted as a
black square (M).

- In every ffmpeg assembly, the first segment gets no `fade=t=in`.
- Check frame 0 before handing a film over: `signalstats` YAVG (a full frame read about 44 here, black about 16), or
  extract it and look.

## A longer film, the way it was made (technique)

1. **Scenario first.** Beats, each with a visual note and the voice line, given for approval before anything is built.
   The script is a plain file, one line a beat: `id|caption|voice`.
2. **Screens by the board's own code on the PC.** A film program built per display with the same sources as the
   preview, using its PNG writer and its map tiles. Each clip goes to an MP4 at the glass's own size (yuv444p, crf 6,
   near lossless for the later steps) plus PNG stills. The finger overlay comes from tour.c (glide, press, away). The
   weather is a sample day written for the film, the planes the demo sky. Wait out anything that covers the screen in
   the first seconds: a new-warning toast hid the dial, so the clip starts 5 s later.
3. **Site shots** from a test copy of the site with a fake board (a fake Web Serial port that answers like a board):
   an element of the page rendered to an image in the page (html-to-image) and posted to a small local sink that saves
   it. Grab the inner panels, not the window: the window's absolute position shifted the grab. Capture at the size the
   page lays out at: a page scaled to fit came out blurred.
4. **Slides** drawn by a C# tool (System.Drawing) at 1920x1080 in the site's look: a footnote "the board's own code,
   drawn on a PC" top right; the end card with the QR, the links and the data credits (Open-Meteo, OpenStreetMap).
   Clips are placed into rooms on the slides; a top layer PNG can go over a clip. Rings in the site's amber round the
   controls to press on site stills. A C# trap met there: a method named `Console` shadows `System.Console`.
5. **Voice** by a local text-to-speech, sentences under about 150 characters, the phonemes checked for odd words.
6. **Assembly** with ffmpeg: one segment per beat with a 0.3 s lead and a 0.45 s tail and dip-to-black fades, beat 01
   with no fade-in. Pixel boards scaled by nearest neighbour, the scaled 5" glass by lanczos. Light steps by trim and
   `colorchannelmixer`, a slow dim by `blend` with `all_expr` on T. Before and after: a wipe (a curtain, the version over
   each side), the first frame held so the curtain crosses on the words "Now...". The final pass `loudnorm` to -16 LUFS.
7. **A console on camera is one session**: opened once, every command in turn with nothing cleared, straight cuts
   between commands, only the title and the command strip change. Not a pile of slides.
8. **Keep the film's tools in the repo** once the design settles: scratch folders get cleaned, and a film's rig was
   lost that way once.

## Stills

- Preview shots are the stills: the README's pictures, the site's board cards, the guide pictures (tools/Guide,
  pc-preview-tool.md) are all rendered, never photographed.
- A still from a film run: the film program writes PNG stills beside each clip.
- Weather stills rendered at the size of each display, with the panel and without.
- A photo of the real glass is still worth having for e-paper and for colours: it shows contrast the preview does not
  (e-paper-design.md).
