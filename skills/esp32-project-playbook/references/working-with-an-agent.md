# Working with an agent: how these projects are made

The method behind OWLOS Flight Radar and its neighbours, as a maintainer and an AI agent worked together from
2026-11-04 on. The maintainer decides, reviews and commits; the agent researches, builds, tests on real boards,
measures and writes it down. Everything below was done this way; the rules in SKILL.md ("Working rules for an agent
beside a maintainer") are the short form, this is the long one with the reasons.

## The loop

1. **A wish in a few words** from the maintainer ("weather on the radar", "the history of versions at the bottom of
   the site", "the circles are 7 mm wider on a 7 inch").
2. **The agent reads first**: the code, the skills, the earlier notes. An answer about what the product does comes
   from grepping the code, not from memory.
3. **Talk before building** when the direction is new. Give two or three ways with one recommended, and say what
   each costs (memory, frames, a server, a promise). "Don't build, let's discuss" means exactly that.
4. **Show choices the maintainer can see**: HTML mockups side by side (A instruments, B sky, C a 24-hour dial), a
   still from the preview, a short clip, a scenario of beats. They pick; often they pick a mix ("Gource plus the
   dial").
5. **Build the pick in the product's own code**, not in the mockup's: the mockup was only for choosing.
6. **Test it without the maintainer**: preview shots, a pixel compare, the board on the bench, the site in the
   agent's browser pane with a made-up board (below).
7. **Show the result** (a still, a clip, a local server they open), with the numbers measured.
8. **Their reaction becomes a rule**, written into the skill the same day (below).
9. **The working tree is handed over.** The agent never commits or pushes, and does not offer to. The maintainer
   reviews and commits.

## Design is done in the open

The look is the product's selling point here ("people value us for this"), so it gets the most turns. What the
turns taught, each written into esp32-ui-drawing or project-showcase where it belongs:

- **Start from the product's own shape.** A radar is radial: its weather became a 24-hour dial, its history a dial
  and then a tower standing on that dial. Rows and axes came only where the data is a line in time.
- **The golden section where proportions are free**: the words over a graph take 0.382 of the height, the graph
  0.618; "now" stands at 0.382 of the width; the dial's core is 0.382 of it; items round a dial a golden step apart.
  The maintainer's words when a layout drifted: "golden section! we keep to it."
- **Weight goes on what moves**; grey for what never changes; one colour one meaning.
- **Data screens stand still**: no sweeping beam over weather. A radar beam belongs to the radar.
- **Smooth, soft, alive where it is a picture**: "more smoothing, animation, halftones, blur, physics; it's a
  radar". A diagram that moves linearly reads as dead.
- **Light saturates where it is dense**: "very much and very bright, just white dots". Light the one thing looked
  at, dim the rest, and tune the dim level between "too bright" and "too dim".
- **Every pick has a way back** ("picked one, can't get back").
- **Use the product's own interface**: "full screen" meant the site's own window maximized, not the browser's.
- **On e-paper the drawing is its own**: colour-screen tricks do not work on ink; halftones, never black clouds, no
  LCD frames.

How to take a design remark: do not argue the taste, find the cause in the picture (often measurable: brightness of
a layer, how many marks overlap, how fast something moves), change that, and show the same view again.

## Measure, don't believe

- **Before and after on the same thing**: the same sky, the same board, the same settings. Frame time and heap from
  the board's 10 s log, not from a guess.
- **Measure what the eye sees.** Planes that "wobble" passed a test that measured jumps; the wobble was sideways and
  backwards motion of the nose, found only by replaying a recorded real sky and counting pixel steps back.
- **Real data with a known truth**: recorded server answers replayed on the PC, a day of weather saved as a sample.
- **A pixel compare proves nothing else changed**: the preview's shots before and after; drawing in strips or halves
  compared with drawing whole, 0 pixels apart.
- **When reading the code does not settle it, make the picture answer**: a throwaway build that draws the suspected
  value is faster than an hour of reasoning.
- **Say how sure each fact is** (measured, built, docs, reasoned), in chat and in the skills.

## The agent tests, not the maintainer

Never ask the maintainer to click something and report. Build the rig instead.

- **A board on the desk** is driven over its USB serial port: identify it by chip and MAC, back it up, flash it,
  read and set its settings with the line protocol (`OWLOS GET`, `OWLOS SET {...}`, `OWLOS KEY n`), read its log,
  put every setting back as it was. The rigs: esp32-runtime, bench-rigs.md and testing.md.
- **The site** runs in the agent's own browser pane, from a test copy on a local server, against a made-up board on
  a fake Web Serial port (esp32-flash-and-settings, fake-board-testing.md). The maintainer's own browsers are never
  started.
- **Pictures from a hidden pane** go through a local sink, with time stepped by hand (project-showcase,
  capture-in-a-hidden-pane.md).
- **What only a person can see** (the colour of the glass, an LED, the feel of a touch) is asked precisely: which
  board, which screen, what to photograph.
- **Passwords are typed by the person** into a rig that asks for them; the agent never sees, sends or stores one.

## More than one agent

The lead agent keeps the thread with the maintainer and the decisions; helper agents take work that stands alone.

- **When**: reading many files for one answer, independent pieces of writing, a review from eyes not anchored on
  the lead's view. Not for a lookup the lead can do in a few calls.
- **A brief is everything the helper knows**: the goal, the files to read, what is already ruled out, the exact
  folder to write in, the style, the hard rules (no private values, do not flash, do not run rigs, do not touch the
  repository). For several helpers, one shared brief file they all read.
- **One concern or one folder each**, never two agents on the same file.
- **The lead checks what comes back** before it is believed or handed on: a helper's confident summary can be
  wrong.
- Done so here: the skills restructured into one file per board, chip, panel and service by six agents from one
  brief (2026-11-23); a whole-code review after a big merge by several read-only agents, one concern each (memory,
  board differences, the site, the settings); the bench rigs cleaned for this collection by one agent while the
  lead wrote another skill (2026-11-25).

## What was learned goes into the skills, the same day

- A finding that would help the next person goes into the right file of this collection, with its how-sure word,
  when it is found, not at the end of the project.
- The collection is the source; a repository's `skills/` is a copy, refreshed from it.
- Tools that made a result (a film's kit, a bench rig, a test page) are kept in the repository or in a skill's
  `files/`: scratch folders get cleaned, and a film's whole rig was lost that way once.
- **No private value ever reaches a skill**: no home, network name, key, address, MAC, port, local path, email,
  person's name beyond a public handle.

## Small things that matter

- **A small fix means a small fix**: the named change only; the rest is proposed in a line.
- **A held beta is frozen**: a new ask goes on the next version's list with a drafted reply.
- **Consequences, not guardrails**: propose the honest mechanic even when it is harsh; an off switch is the
  maintainer's call, said in chat, never done silently.
- **One link, the newest file**: when something is made again, give one path and remove the old copies.
- **Chat in the maintainer's language, everything public in English.**
- **Kill only your own processes, never drive the maintainer's desktop.**
