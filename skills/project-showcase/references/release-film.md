# The release film

How the OWLOS Flight Radar 0.16 release film was made on 2026-11-24: 3 minutes 3 seconds, 21 beats, 1920x1080,
31 MB. It merged two beta films (0.16.1 and 0.16.2) into one for the stable release, added the site's history
window, and ended on a look at the next version. The kit is `files/film/`. The technique of the board's own films
(tour.c, the finger, captions) is in esp32-ui-drawing, films-and-stills.md; this file is the whole film around it.

## 1. The scenario, approved first

A plain file, one line a beat: `id|caption|voice` (`files/film/script.txt`). In chat, each beat also gets a short
visual note in the maintainer's language, so they approve what is seen, not only what is said. Nothing is built
before the yes. For 0.16 the maintainer also chose, from two options given, to end on a look at 0.17.

The shape that worked, 21 beats:

- 01 the name and one sentence of what is new;
- 02 to 08 the biggest new thing first (WEATHER: the dial, its pages, warnings, units, the screen's light);
- 09 to 12 what got better (the map, gliding planes, both cores) with before and after;
- 13 to 16 the site and the console, then how many boards;
- 17 and 18 the project's history window;
- 19 what "stable" means for a board already in a drawer;
- 20 what is coming, only what the maintainer decided;
- 21 free and open, where to flash it, where to talk.

Voice lines: one or two sentences a beat, each under about 150 characters (longer ones lose their tune in the voice
[measured]). Numbers and names spelled the way they must be said: "zero point sixteen", "E S P thirty two C five",
"the U V". Plain words over feature names.

## 2. The board's screens, drawn by its own code

`files/film/render/film5.c` is compiled with the firmware's `core/` and the preview's PNG writer and map tiles, once
per display (`build.cmd 10` the 5" glass, `1` the 2.8", `2` the 3.5"), and run. Each clip goes to an MP4 at the
glass's own size, near lossless (yuv444p, crf 6) for the later scaling, plus PNG stills.

- The home is the repository's default (Trencin), Chicago for the AUTO units beat; the weather a sample day written
  for the film; the planes the demo sky; the WiFi name made up.
- The version is set by the build (`FLIGHT_VERSION`), so the screens say 0.16. A board on the old version saying
  NEW 0.16 is drawn by building with the older number.
- Wait out what covers the screen in the first seconds (a new-warning toast hid the dial: the clip starts later).
- **The next version's screens** come from its own branch without touching the working tree:
  `git archive 0.17 core tools/preview | tar -x -C r017`, then `build017.cmd` builds the same film5.c against those
  files with `HUD017` defined, which draws only what that version adds. The preview's own shot of the branch had
  shown a dated toast; the film's build has none [measured].
- **Clips reused from an earlier film keep their old labels**: relabel them. The beta films' map clips said 0.16.1
  and 0.16.2; for the release they had to say 0.15 and 0.16 (the 0.16.1 map was 0.15's) [measured, caught in review
  of the stills]. A console clip that lists boards on 0.16.2 was kept and the maintainer was told.

## 3. Before and after

`files/film/mapclips.sh`: two preview shots of the same place, old core and new, a curtain going across from the old
to the new with a bright line at its edge, the version written over each side. In the assembly the clip's first
frame is held so the curtain crosses on the words "Now..." of the voice.

## 4. The site and the console

- Shots of the site come from a test copy with a made-up board (esp32-flash-and-settings, fake-board-testing.md),
  at the size the page lays out at.
- **A console on camera is one session** (the maintainer's words: "opened the console, the boards, typing commands,
  getting results, not a pile of slides"): opened once, every command in turn, nothing cleared, straight cuts
  between beats, only the caption changes. `files/film/shellclips.sh` crops the boards and the console from the
  page's frames and stacks them, the console at twice the size so its text reads.
- The history window's clips were filmed with time stepped by hand (capture-in-a-hidden-pane.md), maximized, with
  a version picked and the code growing.

## 5. The slides

`files/film/SlideBake/` (C#, System.Drawing, `dotnet run -- <film folder>`): 1920x1080 slides in the site's look,
a dark ground with a faint grid, the name in mono, the beat's caption, frames with their corners cut and lit. A
frame left empty is where a clip goes: its place is written to `places.sh` for the assembly.

- A footnote top right says what the picture is: "the board's own code, drawn on a PC", "the site, with a made-up
  board", "the site's own history window, drawn from the project's git log".
- The end card: the QR code (made by the radar's own `tools/QrBake/Qr.cs`), the site, the source, the group, and the
  data credits (Open-Meteo, OpenStreetMap contributors).
- Never "made in": the map shows where it lives.
- A C# trap met: a method named `Console` hides `System.Console`; name it something else.

## 6. The voice

A local text-to-speech (Kokoro, the voice am_michael at speed 1.20), one WAV a beat. Check a line's phonemes
before rendering when it has a name or a number. The final mix is normalised to -16 LUFS.

## 7. The assembly

`files/film/assemble.sh` (ffmpeg, Git Bash): a segment a beat, then one encode of them all.

- Each segment: its slide, its clip scaled into its place (nearest neighbour for pixel boards, lanczos for the 5"
  glass), its voice after a 0.3 s lead, 0.45 s of tail, a 0.3 s dip to black at its end.
- **Beat 01 has no fade-in**: the first frame is the picture, never black. The last beat holds 4.5 s and fades for
  1.2 s.
- Helpers inside it: `one` (a slide and a clip, from a second into the clip, its first frame held, a top layer, a
  crop), `still` (a slide alone), `two` (two slides, the second from a moment in the voice).
- Light steps on a screen (the backlight at 100, 50, 25, 10) by `trim` and `colorchannelmixer`, a slow night dim by
  `blend` with an expression on time.
- The console's two beats are one session: no fade between them, a `null` filter.
- Two clips side by side for the next version's two boards.
- The end: H.264 crf 18, AAC 160k, `+faststart`; the first frame written out as a PNG and its average brightness
  printed (`signalstats` YAVG: about 44 for a full frame here, black is about 16).

## 8. Before handing it over

- Look at a contact sheet of every beat's middle frame: wrong labels, a dated toast, a private name on a map all
  show there.
- Frame 0 is a picture, the length is what the voice needs, the sound is even.
- Give one path, the newest. Keep a copy outside the scratch folder (scratch folders get cleaned, and a film's rig
  was lost so once).
- Say what was left as it was and why (the console listing 0.16.2 boards here), so the maintainer decides.
