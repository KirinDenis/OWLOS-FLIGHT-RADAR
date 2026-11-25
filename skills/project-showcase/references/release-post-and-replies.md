# The release post, and replies to people

What goes out with a version, and how a question from the group is answered. Taken from OWLOS Flight Radar 0.16
(2026-11-24 and 25).

## The post

Short, for a group where people scroll: the version, what is new in a list, where to flash it, what happens to a
board already flashed, where the source is, what comes next, the credits. Written by the agent, given in chat ready
to paste, posted by the maintainer with the film. The 0.16 post as it went out:

```text
OWLOS Flight Radar 0.16 is out.

The planes around your home on a small ESP32 board with a screen. New in 0.16:

- WEATHER, a fourth look: the next 24 hours on the radar's own dial, pressure and temperature pages, warnings on every look
- The units your country uses: °C or °F, hPa, mmHg or inHg, four kinds of wind
- A screen that dims at night, by the sun over your home
- A map you can read on a small screen: clearer land and water, whole roads, the map's own town names
- Planes that glide instead of jumping
- Both cores drawing, on every board that has two
- A console on the site that runs your boards
- 17 boards, new and experimental: ZLX-ESP32-2
- The project's own history at the bottom of the site: every commit, every version, every board

Flash it from your browser (Chrome or Edge, a USB cable):
https://kirindenis.github.io/OWLOS-FLIGHT-RADAR/

A board on 0.15 shows NEW VERSION by itself, and your settings stay.

Source code, free and open:
https://github.com/KirinDenis/OWLOS-FLIGHT-RADAR

Coming in 0.17: a new weather display for the 2.8" and 3.5" boards.

Weather: Open-Meteo. Maps: © OpenStreetMap contributors.
```

What makes it work:

- **The first line says it all**: the name and "is out". The second says what the thing is, for a stranger.
- **Features as a person sees them**, not as the code names them: "a screen that dims at night", not "NIGHT
  backlight option".
- **Every claim checked against CHANGES.md and the code** before the post: the number of boards, which are
  experimental, which browsers flash.
- **What happens to someone who already has one**: the NEW VERSION note, settings kept. People ask this first.
- **"Free and open" once**, next to the source link. The project is a skill given away, not nostalgia and not a
  product launch: no "revolutionary", no "first", no comparison with anyone.
- **Coming next only when decided**, in one line.
- **Credits** for the data and the maps shown on the screen.
- No emoji, no hashtags, no long dashes, no lines about AI. Nothing promised for a date.
- If the post goes outside the project's own group, a last line points to the group.

## Replies to questions

A question from the group gets a reply drafted by the agent and posted by the maintainer. The method:

1. **Read the code before answering.** What does the firmware do today, in which case, on which boards? Quote no
   memory of it; grep it.
2. **Answer what it does now**, plainly, with how to get it.
3. **Say why when it cannot**, in a sentence a non-programmer follows. No blame on anyone's service.
4. **Say what is planned only if the maintainer decided it.** If it is only an idea, the last paragraph is a soft
   "we are looking at it", and the agent tells the maintainer that this paragraph is a promise to be kept or cut.
5. **Thank first**, in a word. Keep it short.

Example: "Is there any chance to display source and destination of selected flight?" (2026-11-25, the second time
this was asked). The code showed the panel prints `ORIG > DEST` only when the plane came from Flightradar24, the one
source that sends a route; the free ADS-B networks send none. The reply:

```text
Thank you!

It does already, with your own Flightradar24 API key: tap a plane, and under its callsign the panel shows where it
comes from and where it goes, as airport codes (VIE > LHR).

The free ADS-B networks can't give that. They pass on only what the plane broadcasts about itself: its position,
height, speed and callsign. A route isn't part of that.

For the free networks we are looking at open route data, looked up by callsign, for the one plane you tap. Some of
these routes are out of date, so the board would show one only when the plane really is flying it.
```

Beside it, for the maintainer: the plan in three lines (the open route data per callsign, about 0.8 KB, fetched only
for the plane picked; shown only when the plane flies along that route; every board, no server of our own), and the
question whether it goes into the next version.

## What a question is worth

- A question asked twice is a feature people want: say so to the maintainer, with the date of the first ask.
- A board asked for goes into BOARDS-WANTED.md (esp32-project-playbook, board-requests.md); nothing is ported until
  the maintainer says.
- Feedback with a measurement ("the circle is 7 mm wider on my 7 inch") is a fact for the board's file, marked as
  measured by its owner, and goes on the next version's list. The owner's photo stays private: nothing from it about
  where they live is kept.
