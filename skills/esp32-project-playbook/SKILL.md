---
name: esp32-project-playbook
description: How an ESP32-with-a-screen project is run from first commit to a group post, and how its skills are given away. One repository per project with the skills that built it in a skills/ folder and an AGENTS.md, the order of work, repo layout, working rules for an AI agent beside a maintainer, public text and privacy rules, people's board requests (BOARDS-WANTED.md, port none until the maintainer says), pull requests and forks from people (review by what reaches other boards, squash, credit by public handle), the beta then stable release routine with CHANGES.md and version.json, the version archive on the site, and the build PC's quirks (ESP-IDF and PlatformIO through cmd, not Git Bash; Emscripten). Use when starting a new ESP32 project, before writing anything public (README, site, CHANGES, an issue reply, a group post), when someone asks "will it run on X", when a pull request or fork comes in, before a beta or a release, or when a build, a publish or a CI run goes wrong.
---

# Running an ESP32 project of this family

The process behind OWLOS Flight Radar, OWLOS WiFi Radar, OWLOS Thermo Relay, OWLOS Air Quality UI, ESP32 Web
Flasher and a contribution to FusionEdge. This file is the process; the other skills in this collection are the
technique. Written 2026-11-04, brought up to date 2026-11-23.

## Where to go

- [references/board-requests.md](references/board-requests.md): people's requests and questions, the
  BOARDS-WANTED.md entry, reading what people send, answering.
- [references/contributions.md](references/contributions.md): pull requests and forks from people, licences,
  credit, working in someone else's project.
- [references/releases.md](references/releases.md): one version number, the beta channel, holding a beta, the
  archive of versions, a new board's list of places, publishing in order, CI on pull requests.
- [references/build-pc.md](references/build-pc.md): ESP-IDF, PlatformIO, Emscripten, boards on the desk, the browser
  pane, small traps.

The technique: [esp32-boards](../esp32-boards/SKILL.md) (chips, every board, porting and researching one),
[esp32-lcd-touch](../esp32-lcd-touch/SKILL.md) (panels, buses, touch, e-paper),
[esp32-flash-and-settings](../esp32-flash-and-settings/SKILL.md) (esptool, the browser flasher, settings over USB,
CI, versions), [esp32-ui-drawing](../esp32-ui-drawing/SKILL.md) (the renderer, fonts, themes, layouts, the PC tools),
[esp32-runtime](../esp32-runtime/SKILL.md) (tasks, memory, WiFi, TLS, storage, crashes, testing without a person),
[esp32-data-sources](../esp32-data-sources/SKILL.md) (flight data, maps, weather, sensors).

How sure each fact is, here as in every skill: **measured** (seen happen here, on a real board or a real run),
**built** (in the code, not seen run), **docs** (read from a maker, a service, a contributor's notes), **reasoned**
(worked out, not checked). A rule with no tag is a decision.

## One repository per project, skills that travel

Decided 2026-11-04 after weighing an OWLOS operating system with loadable apps: too heavy. It would have needed
boards and screen sizes at run time, a stable API, and a flash layout for one app at a time on a 4 MB board. So
each project is its own repository with its own copy of the code, and what one project learned goes on as a skill.
Do not propose a shared core, a launcher or an app loader for these projects.

To start a new one:

1. Copy the newest project that is closest (Flight Radar is the most complete and the most current) and delete
   what the new one does not need.
2. Copy the skills it needs from this collection into its `skills/` folder, and write its `AGENTS.md` (below).
3. When the project teaches something new, change the skill in this collection first, then refresh the copy.

## Open Skills

These skills are given away, next to the open code, so that people and their own AI agents start where we stopped:
the know-how that built the firmware, proven on real boards, with every fact saying how sure it is.

- **A repository shows the skills that built it** in a `skills/` folder at its root, one folder per skill in the
  Agent Skills format (a `SKILL.md` with `name` and `description`, details in `references/`).
- **An `AGENTS.md` at the root points an agent to them**: which skill for which job, and to read it before working.
  Agents that look elsewhere (Claude Code reads `.claude/skills/`) can be pointed at `skills/` or given a copy
  [docs].
- **The source of truth is this collection.** A repository's `skills/` is a copy. Fixes and new facts land here
  first; a copy is refreshed from here, never edited on its own.
- **Every fact keeps its how-sure word.** A copy, a summary or a port to another tool never upgrades "built" to
  "measured". A fact that changed says what changed and when.
- **The public text rules apply to the skills too**, the privacy ones first. A skill is read by strangers and their
  agents.

A minimal `AGENTS.md`:

```markdown
# Agents

This firmware was built with the skills in `skills/`. Read the one for your job before you start:

- `skills/esp32-project-playbook`: how this project is run, what may be published, how a release goes.
- `skills/esp32-runtime`: tasks, memory, WiFi, storage, crashes, testing without a person.
- `skills/esp32-boards`: the boards and their pins.

Each fact in them says how sure it is: measured, built, docs or reasoned. The skills' home is the ESP32 skills
collection; improve them there.
```

## The layout

```text
core/        everything that does not touch hardware: the model, the demo data, fonts, drawing, the screens.
             plain C, the same files build the firmware, the PC preview and the web twin
firmware/    ESP-IDF 5.5 project: main/ (board.h, drivers, net, store, console), BUILD.BAT, build.sh,
             sdkconfig.defaults*, partitions*.csv, components/ (vendored libraries with their licences)
web/         the web twin: core/ for WebAssembly, its page
site/        index.html, the flasher, the settings and the console page on GitHub Pages
shots/       PNGs the PC preview draws: the README and the site use them (only those are kept in git)
tools/       preview/ (PC build, tour film), the bakers (fonts, QR, maps), the guide arrows, a fake serial port: C#, never Python
server/      a small service of the project's own, if it has one (the radar's opt-in count of boards)
skills/      the skills that built it, copied from the collection
.github/     workflows/pages.yml, make-manifests.sh, archive.sh
AGENTS.md    points an agent to skills/
CHANGES.md   the first "## " line IS the version, "(beta)" on it makes a beta
BOARDS-WANTED.md   what people asked for and what is known about each board
README.md    what it is, a film, why, how to use it, boards, build it yourself
LICENSE      MIT, a copyright line per contributor
```

## The order of work

1. The screen first, on the PC. `core/` with the preview: every page as a PNG, no board needed. Layout and looks
   are decided here at PC speed. For a new screen, two or three mockups first and the maintainer picks; start from
   what the product already is (the radar is round, so its weather became a dial), and reuse what was already
   approved before inventing a variant.
2. A demo mode with made-up data, labelled DEMO in big letters. It lets every screen be seen with no WiFi, and it is
   what a stranger sees first.
3. The board: one that is on the desk, `board.h`, then touch alignment.
4. The network, then settings from the board itself and from the site.
5. The site with its flash buttons, CI, `version.json`, the console.
6. The film and the arrows on the screenshots, README last, written from what really runs.
7. More boards when people ask: listed first, built at the maintainer's word (references/board-requests.md).
8. New features in a beta, held on real boards, then the stable (references/releases.md).

The heavy thinking stays honest on screen: a thing the firmware does not know is shown as not known. A radar with
no direction draws a hollow ring on its range, a board on a table draws no fake map. On a screen people read for its
numbers, decoration does not move; what moves is the data, and every mark must say what it is.

## People's requests, in short

- Every "will it run on X" goes into `BOARDS-WANTED.md`: what the board really is (chip, screen chip, touch chip,
  pins, where they came from), who asked and whether they can test. **Port none until the maintainer says.** Draft
  the reply at once.
- Find out what X really is before answering: shop titles are noise, and a look-alike board may have other pins.
- Replies are ready blocks in English, in the maintainer's first person, with direct links, the real mechanism in
  the right word, and no promise of a date.

Detail: [references/board-requests.md](references/board-requests.md).

## Contributions, in short

- Review a pull request by what it does to the boards its author does not have: keep the general fixes, gate the
  board-specific changes under the contributor's own switch with the old code in `#else`, and take out what was
  written for a board they do not have, any HTTPS gate on NTP, and duplicate build files.
- A fork whose history carries GPL files or large media comes in squashed, never merged with its history.
- Credit by public handle: `LICENSE`, README, CHANGES. The integration work is the project's.

Detail: [references/contributions.md](references/contributions.md).

## Releases, in short

- The first `## ` line of `CHANGES.md` is the version; `(beta)` on it makes a beta, kept on the site beside the
  stable. `version.json` carries `version` (the stable, every board reads it) and `beta` (only beta builds follow).
- A beta is held a few days on real boards, frozen for features, then becomes the stable by losing `(beta)`.
- The site keeps every version's firmware; a lower number or a missing old file stops the workflow.
- Build, check sizes, flash a real board, assemble the site locally, the maintainer pushes, check the run and the
  live files from outside, and only then post.

Detail: [references/releases.md](references/releases.md).

## The build PC, in short

- Windows with Git Bash and PowerShell. Run ESP-IDF's `BUILD.BAT` and PlatformIO through PowerShell `cmd /c`, never
  from Git Bash (`idf.py` is not found, PlatformIO stops with "MSys/Mingw is not supported").
- No Python tools, no `gh`, no Docker running: the REST API anonymously, `curl`, `tar.exe` for zips.
- Boards by MAC, never by port number. Backups and rigs in a folder that lasts.

Detail: [references/build-pc.md](references/build-pc.md).

## Working rules for an agent beside a maintainer

Standing rules; they do not need saying again.

- **Never commit or push, and never offer to.** The maintainer reviews the code and commits. The end of a task is
  "here is what changed in the working tree". A loose "do what is best" is not permission; only a plain, specific
  ask is, and only for what it names.
- Chat in the maintainer's language; everything in a repository in English.
- **A small fix means a small fix**: make the named change, propose the rest in a line.
- **Agents test, not the maintainer.** Never ask them to click something and report what happened; build the rig.
  If only they can see a thing (a CI log behind a login, the colour of an LED, a photo of the glass), say exactly
  what to send.
- **Do not drive their desktop**: no injected keystrokes, no starting their applications. Kill only processes you
  started.
- **Before flashing**: identify the board by chip and MAC, never trust a port number from last time, back up a
  board that holds something, confirm all hashes verified after. Ask before flashing a board that is not plainly
  the one in the task.
- **Passwords and keys** are typed by the person, into the board or a rig that asks for them. The agent never sends
  one, never prints one, never stores one in a file.
- **No Python**: tools are C#. In C#: types written out (no `var`), plain conventional code over clever. In C# and
  JS every if, else and loop body in braces.
- **New boards**: add the ask to BOARDS-WANTED.md and draft a reply; port only when told.
- **A held beta is frozen for features**: a new ask goes on the next version's list, with only the reply drafted.
- **Standard terms**, with a short gloss the first time, and a gentle one-line correction when a word is off.
- **Review by chat**: a short question gets a short answer; details only when asked.
- **One link, the newest file**: when a film or a picture is redone, give the one new path and delete the old
  copies, or the reader opens the wrong one.
- **Measure what a person complains about**, on real data, before calling it fixed: a test that passes while the
  eye still sees the fault measured the wrong thing.

## Public text

README, site, CHANGES, posts, issue replies, and these skills.

- English. No emoji, no tables, no long dashes. Plain short sentences, written by a person. No "AI" lines, no
  "generated by".
- Say what is true now: "New: not run on one here yet", never "supported" for an untested build. Say where the pins
  came from.
- Never frame the project against another one, never claim "first" or "best". Every piece has a precedent.
- No invented dates or history.
- No Russian services anywhere.
- Credit data and map sources where they are shown and in the footer: the licences of adsb.lol and adsb.fi,
  OpenStreetMap contributors, Open-Meteo, the flasher's library.
- Credit people by the public handle they use, where the change is described. No email in any file.

## Privacy

- Never the maintainer's or a user's home point, town, village, WiFi name, password, key, LAN address, MAC address,
  port number, local path, or email. Not in code, shots, logs, a film, a skill, or a chat that gets pasted.
- Check screenshots and films for the map's place names: they should show only the repository's default home or a
  made-up one.
- A secret goes into the board and stays there: the board shows only whether one is stored.
- A board's log names its network, its address and its home's town: rigs print booleans and counts, and a log is
  masked before anybody sees it.
- A core dump is a copy of RAM, passwords included: never ask for one in public.
- A plane list or a weather question gives away the home's area: round the home before it leaves the board (0.01
  degree to plane servers, 0.1 to the weather), and never put it in a URL that is logged.
