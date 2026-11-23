# People's board requests and questions

Group posts, issues and comments bring most of the work. What worked, from OWLOS Flight Radar and the projects
around it. How to research a board itself is in esp32-boards.

## BOARDS-WANTED.md: list each ask, port none until the maintainer says

Requests come faster than boards can be bought and tested. So every "will it run on X" goes into
`BOARDS-WANTED.md` at the repository root, and nothing is ported until the maintainer says to. The list decides
what gets built later; a reply is drafted at once.

What each entry holds:

- A heading that says the state in words: `## NM-CYD-C5: built 2026-11-03, first run by a tester 2026-11-06`, or
  `## <board>: asked <date> (issue #N), built <date> as <build>, not run on one yet`, or just the board's name and
  "asked".
- What the board really is: chip and module (flash, PSRAM), screen and its controller, touch and its controller,
  USB (a bridge chip, the chip's own, or both), buttons, LEDs, speaker, sensors.
- The pins, and where they came from: the maker's schematic, the maker's example code, another firmware's port of
  the same board. When the maker's repository is gone, its forks often still have it. Two sources that agree are
  worth saying; two that disagree (RGB or BGR) are a reason for two screen profiles.
- What the build does with it: inversion, the landscape command, the colour order tried first, the bus speed and
  why.
- What is not driven yet (the LED, a sensor, the speaker), so nobody thinks it was forgotten.
- Who asked, where, and whether they offered to test. Only a public handle, and only where the asker posted it in
  public; otherwise "the asker".
- What to learn from the tester: are the colours right, is the picture the right way up, does the touch work after
  the alignment, does the site see the board, do planes come over WiFi, does the picture shake while the map loads.
- The first report, in a line, when it comes: "the screen stopped with not enough memory for the screen", and what
  changed after it.

Rules around it:

- A second ask for the same board is a good reason to propose building it. The build still waits for the
  maintainer's word.
- A board that is "the same" as one already built often is not: a C5 board whose pins were not the other C5 board's
  would have had a dark screen with that build. Check the pins against the maker's own pin file before saying
  "pick that card".
- A built board no one here has run is written everywhere as "New: not run on one here yet": the README, the site's
  card, `board.h`, this list. It stays so until its owner reports.
- A board in a beta only: say so on the site and in the reply. It gets the beta until the stable has it.
- A request for a feature rather than a board is noted the same way, for the next version, with the reply drafted.

## "Will it run on X"

- First find out what X really is. A shop title is noise: the same panel is sold under many names, and a shop option
  can be only an add-on module. A board sold as a well-known one may be another maker's with different pins: two
  "2.8 inch CYD" boards on the bench turned out to be another maker's ESP32-32E boards, found from a photo of the
  back. Check the chip, the screen chip, the touch chip. Often it is a known board and the answer is "pick that card".
- All a build needs is the type and the pinout. Say that, and ask for it.
- Ask the owner of the board what only they can see: are the colours right, is the picture the right way up, does
  the touch work, does the site see the board.
- A library or project someone points to is a data source, read only. Check whose code each file is before
  copying any of it (one such library carried another author's files inside), and check the licence.

## Reading what people send

- A wiring drawing from a comment is tiny. Enlarge it (System.Drawing in PowerShell, bicubic, 6x), trace by the order
  of the wires, and never trust the digits: send them back in text and ask for confirmation.
- A comment you can't parse: state the two readings, answer with a question that settles it.
- A screenshot of a log may hold a network name, an address or a town: do not repeat them.
- A tester's video of a fault: try to make it happen with a rig on the bench before changing code. If the bench
  cannot make it happen on the same version and the version before, ask for the tester's version and the relevant
  log lines; say plainly that it depends on their place or their board till those come.

## Answering

- Give people direct links, every one of them, not a template with `<board>` to fill in. They will not fill it in.
- Explain the hard parts truthfully and in the right word. The WiFi Radar does trilateration from a moving point, not
  triangulation: one antenna measures no angle. "In a way, yes" plus the real mechanism beats a flattering yes, and
  the person asking usually knows the difference.
- When the honest answer is "no source for that", say so: the free plane networks give no route, only a position,
  course and speed; a route needs another source and its licence read first.
- Replies come as a ready block in English, in the maintainer's first person, for the maintainer to paste. No
  Markdown for Facebook, Markdown for GitHub.
- An issue about a screen that looks wrong: first suspect is the wrong screen profile, the BOOT press in the first
  8 seconds.
- A person who suggested an idea that was built gets the credit for the idea, in their public handle, where the
  project says what changed.
- Never promise a date. "In a coming beta" only when the maintainer said it.
