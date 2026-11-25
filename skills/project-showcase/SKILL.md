---
name: project-showcase
description: How an open project shows itself to people, as OWLOS Flight Radar does. It covers four things. The project's own history drawn from git on its site, in one window with three views: a tower of time in WebGL with three.js, the code growing in the manner of Gource, and the versions on a dial. A release film whose screens are drawn by the firmware's own code on a PC, with slides in C#, a local narrated voice and ffmpeg. Capturing a page's animation frame by frame in a browser pane that is hidden. The release post and replies to people's questions. Not tied to ESP32. Use when a version goes out, when a project wants its history on its site, when a film or a post is to be made, or when someone asks what the project can do.
---

# Showing a project to people

A project that is given away free must also show what it is, or nobody finds it. Here that is four pieces, all
made by the agent with the maintainer, all checked by the agent before anyone else sees them:

1. **The history on the site**: the project draws itself growing, from its own git log, version by version.
2. **The release film**: a few minutes, the screens drawn by the firmware's own code, a voice, the links at the end.
3. **The release post**: what came out, what is new, where to flash it, where the source is.
4. **Replies to people**: what it does today, what it cannot do and why, what is planned, nothing promised that
   the maintainer did not decide.

How sure each fact is, as in every skill of this collection: **measured** (seen happen here), **built** (in the
code, not seen run), **docs** (read), **reasoned** (worked out, not checked). A rule with no tag is a decision.

## Where to go

- [references/history-from-git.md](references/history-from-git.md): the data. `history.sh` in the site's workflow,
  its line format, versions from CHANGES.md at each commit, why a version only steps forward, names but never
  emails.
- [references/history-window.md](references/history-window.md): the window. The three views (TOWER, CODE, BOARDS),
  how each is drawn, the road the design took with the maintainer's words at each turn, the controls, the tests.
- [references/capture-in-a-hidden-pane.md](references/capture-in-a-hidden-pane.md): stepping a page's time by hand
  and saving each frame, for a film or for a test, in a browser pane nobody is looking at.
- [references/release-film.md](references/release-film.md): the film from scenario to MP4, as the 0.16 release
  film was made.
- [references/release-post-and-replies.md](references/release-post-and-replies.md): the post that goes with the
  film, the reply to a question, the words to use and the ones to avoid.

The files:

- `files/history.sh`: the history for the site, from git.
- `files/site/`: the window's code as it runs on the radar's site: `history.js` (the window, the panel, the dial),
  `growth.js` (the code growing), `tower.js` (the tower, three.js r170), `win.js` (the site's own windows). It also
  needs three.js r170's `three.module.js` (MIT) beside them in `vendor/`.
- `files/capture/`: `frame-step.js` (time stepped by hand, a frame saved), `sink.ps1` (the local folder the frames
  land in).
- `files/film/`: the 0.16 film's own kit. `script.txt` (the scenario), `render/` (the screens drawn by the radar's
  core on a PC), `SlideBake/` (the slides, C#), `assemble.sh` (ffmpeg), `mapclips.sh` (a before and after curtain),
  `shellclips.sh` (a console session cut into beats).

## The rules that hold for all four

- **Scenario first.** A film is a list of beats, each a visual note and a voice line, approved before anything is
  built. A post is given in chat, ready to paste, before it goes anywhere.
- **Show, don't describe.** For a new picture give choices the maintainer can see (two or three mockups, a still, a
  short clip), take the one picked, and build that.
- **Everything is drawn, nothing is photographed or recorded by a person.** The board's screens come from its own
  code on the PC, the site's from the site itself with a made-up board, the history from git.
- **The first frame of a video is a full picture**, never black: the networks take it as the thumbnail [measured].
- **Only what is true today.** A board built but never run is "New: not run on one here yet". A coming version is
  "coming", and only when the maintainer decided it.
- **No private values anywhere**: no home but the project's default one (here Trencin, Slovakia, on purpose), no
  network names, keys, addresses, MACs, ports, local paths. A made-up WiFi name in a film is fine and is plainly
  made up ("HomeWiFi").
- **People by their names only**, never an email, in the history as everywhere.
- **No "made in"**: the map shows where the project lives, people work it out [the maintainer's word].
- **Public text style**: plain English, short sentences, no emoji, no tables, no long dashes, no lines about AI.
- **The newest file only.** When a film is made again, give the one new path and remove the older copies.

## The order when a version goes out

1. CHANGES.md has the version, with its one-line summary under the heading (the history window reads it).
2. The site's workflow publishes it; its history comes from the same run (`history.sh`).
3. The film: scenario, approval, screens, slides, voice, assembly, first frame checked, length checked.
4. The post, with the film, the flasher link and the source link.
5. Replies to questions as they come, each one checked against the code before it is written.

The release mechanics (beta, stable, the archive of versions) are in
[esp32-project-playbook](../esp32-project-playbook/references/releases.md); the board's films and stills by tour.c
in [esp32-ui-drawing](../esp32-ui-drawing/references/films-and-stills.md).
