# ESP32 skills: an Open Skills collection

What we learned building ESP32 firmwares with a screen, given away so that people and their own AI agents start
where we stopped. It comes from OWLOS Flight Radar (19 builds, from a 1.28" round ESP32-C3 to a 10.1" ESP32-P4),
OWLOS WiFi Radar, OWLOS Thermo Relay, OWLOS Air Quality UI, ESP32 Web Flasher and FusionEdge.

The code of those projects is open. These skills are the part that is usually lost: why a pin was picked, which
start-up a panel really wanted, what ran out of memory and how it was fixed, which web service refuses a small
board, how a screen was designed. Each fact was learned on a real board or says that it was not.

## How sure a fact is

Every fact carries one of four words.

- **measured**: ran on that real board here, seen on the glass or in a log.
- **built**: compiled for that board, never run on one.
- **docs**: read from a maker, a port or a datasheet.
- **reasoned**: worked out, not tried.

Believe a measured fact, check the others before you rely on them. A board that was only built is "New: not run on
one here yet", never "supported". When a fact changed, the skill says what changed.

## How to use them

Each folder is one skill in the Agent Skills format (agentskills.io): a `SKILL.md` with a `name` and a
`description`, the general part in its body, the details in `references/`, one file per thing. An agent reads the
descriptions, opens the `SKILL.md` that fits the job, and then only the reference files it needs.

- Claude Code: copy the folders into your repository's `.claude/skills/`, or into `~/.claude/skills/` for every
  project.
- Other agents: put them where your agent looks for skills, or in a `skills/` folder with an `AGENTS.md` at the
  root that points to it.
- A repository built with them shows them in its own `skills/` folder. This collection is the source of truth;
  those are copies.

## The skills

- [esp32-project-playbook](esp32-project-playbook/SKILL.md): start here. The order of work, the repo layout,
  public text and privacy, people's board requests, contributions, the beta and stable releases, the build PC.
- [esp32-boards](esp32-boards/SKILL.md): the chips (ESP32, S3, C3, C5, P4), one file each, and the boards, one
  file each, with pins, the board.h block and sdkconfig. How to port a new board, how to research one nobody has
  on the desk, how to tell which board is plugged in.
- [esp32-lcd-touch](esp32-lcd-touch/SKILL.md): the buses (SPI in strips, the S3 RGB bus, MIPI-DSI on the P4),
  one file per panel controller, per touch chip and per e-paper, the touch alignment, I2C, buttons and LEDs.
- [esp32-ui-drawing](esp32-ui-drawing/SKILL.md): the graphics. A software renderer in strips with no LVGL,
  anti-aliasing, fonts, themes, animation, layouts for every size, touch UX, maps, e-paper design, radial dials,
  data screens, the PC preview and films. One file per topic.
- [esp32-runtime](esp32-runtime/SKILL.md): the project skeleton, tasks and watchdogs, memory and PSRAM per board
  with measured frame times, WiFi and TLS, storage, crashes, testing without a person.
- [esp32-flash-and-settings](esp32-flash-and-settings/SKILL.md): local flashing with a chip and MAC check, the
  browser flasher over Web Serial, settings over USB and the site's console, NVS, CI, versions, the beta channel,
  the archive of old versions.
- [esp32-data-sources](esp32-data-sources/SKILL.md): the web services, one file each (flight data, weather, air,
  maps, geocoding, time and more), the sensors, one file each, and how a small board reads them.

## Ideas talked about, not built

Kept so they are not lost. None of them is a decision.

- A custom board: the person picks the screen chip, touch and pins on the site, stored in NVS, one build per chip.
- The twin as a remote: the browser twin of Flight Radar is built (`web/` there, the same C in WebAssembly, in step
  with a board over USB). Not built yet: the same over the network, UDP from the board to a server and WebRTC on
  to the browser, so the board needs nothing but UDP.
