# The beta and stable channels

The site keeps the stable version and, when there is one, a beta beside it: the new things first, and it may
still go wrong. A board on the stable never hears of a beta by itself. The person picks it, and can go back.
Source: OWLOS Flight Radar since 0.16 (`firmware/CMakeLists.txt`, `firmware/main/main.c` `update_check`,
`.github/workflows/pages.yml`, `.github/archive.sh`, `site/flow.js`, CHANGES.md and README.md). Real files:
`../files/flight-radar/pages.yml`, `archive.sh`, `CMakeLists.txt`.

How sure: on the live pipeline. On 2026-11-23 the site served the stable 0.15 (16 boards) and the beta 0.16.2
(17 boards: one board, the ZLX-ESP32-2, is in the beta only), and `version.json` said
`{"version":"0.15","beta":"0.16.2",...}` (measured). The bench boards run beta builds and kept every setting
(measured). The page's channel switch was checked in a test copy with a fake board (`?fake=old` shows the
STABLE and BETA chips; measured in the browser); switching a real board through the page was not watched here.

## Why channels

Without them every push of a new number reaches every board as a plain update: the NEW VERSION note on every
screen, the newest on every card. A version full of new things wants a few days on real boards first. The
channels were built before the first beta went out for exactly that reason.

## One word in CHANGES.md

```markdown
## 0.16.2 (beta)
```

`(beta)` on the first `## ` line makes the version a beta. Everything follows from that word:

- **The firmware**: `CMakeLists.txt` sets `FLIGHT_BETA=1`. A beta build follows the site's beta while the site
  has one above its stable, then the stable, which is what a beta found good becomes. A stable build never hears
  of a beta (`ci-and-versions.md`, The board asks). ABOUT says BETA after the version.
- **version.json**: `"version"` stays the stable, because boards up to 0.14 read only it (digits and dots only).
  `"beta"` is the beta, read by beta builds alone. `""` when there is none.
- **The site's folders**: the stable stays in `fw/<board>/`, where every board's ABOUT and the old page look. The
  beta goes to `fw/<version>/<board>/`. The workflow moves this run's board folders there before it makes their
  manifests.
- **fw/versions.json**: the list as before (the stable first, then the older ones), and the beta on a line of its
  own after the list, as `"beta"`. The old page reads the list alone and never sees it.

```json
{
  "versions": [
    { "version": "0.15", "text": "0.15, build 46 (3ddd82a)", "dir": "fw", "boards": [ ... ] },
    { "version": "0.14", "text": "0.14, build 43 (13bf918)", "dir": "fw/0.14", "boards": [ ... ] }
  ],
  "beta": { "version": "0.16.2", "text": "0.16.2 beta, build 51 (afbd574)", "dir": "fw/0.16.2", "boards": [ ... ] }
}
```

## The rules archive.sh keeps

- One beta at a time. A new beta replaces it; its files are not kept.
- A stable run ends the beta.
- A beta needs a number above the stable: "CHANGES.md says beta X, the site has Y: a beta needs a number above
  the stable".
- A new beta needs a number not below the site's beta (the same number replaces it).
- A beta's run needs a stable on the site to keep beside it, or it stops.
- A beta's run did not build the stable: it fetches the stable's files from the live site into `fw/`, makes their
  manifests again with the stable's own text, and puts the stable first in the list.

Any of these stops the job before deploy, and the site stays as it was.

## On the page

```js
let newest = '0';          // the stable: what version.json says every board
let beta = '';             // the beta beside it, '' when there is none
let betaBoards = [];       // the boards the beta was built for (fw/versions.json)
let stableBoards = null;   // and the stable's. null: every board

const inStable = (build) => !stableBoards || stableBoards.includes(build);

/* the version a board goes to: one above the stable runs a beta and follows the betas, or the stable a
 * beta became. so does a board the stable has no build for. every other board follows the stable */
function follows(u) {
  const ahead = u.version && (older(newest, u.version) || !inStable(u.build));
  return beta && ahead && betaBoards.includes(u.build) ? beta : newest;
}

/* where a version of a board's firmware is on the site: the stable in fw/<board>, a beta in its own folder */
const fwDir = (build, ver) => (ver === newest ? `fw/${build}` : `fw/${ver}/${build}`);
```

- **A new board**: the board's card, then a Version row with STABLE and BETA, STABLE pressed. The beta is picked
  by the person, never by the page: "A beta brings the new things first, and it may still go wrong. 0.15 is one
  flash away."
- **A board new in the beta** (no stable build for it): the site flashes it the beta, with no row to pick, and a
  note: "This board is new in the beta: there is no stable version for it yet." It follows the betas till the
  stable has it too.
- **A board already flashed**, with any version: in its window SETTINGS, BOARD, Version, STABLE or BETA, then
  FLASH. No erase: its WiFi, home and settings stay. Before 0.16.1 only a new board could pick the beta, and a
  board on the beta had no button back.
- **On the beta** the board sees the next beta by itself and shows NEW VERSION, as a board on the stable does for
  the next stable.
- **Back to the stable**: the same row, STABLE, then FLASH.
- **The console**: `flash` writes the version the board follows, `flash --board NAME` one board of several.
- The page reads `version.json`, and only when it names a beta (digits and dots) does it read
  `fw/versions.json` for the beta's boards and the stable's.

## The routine

1. New things go out as a beta: `## 0.16.1 (beta)` at the top of CHANGES.md, push. Beta steps get numbers of
   their own: 0.16.1, then 0.16.2.
2. Hold the beta a few days on real boards. Only fixes go in meanwhile. A fix to a beta just out may keep its
   number: the same number replaces that version's files on the site, and boards that took the first build get
   no note. Say so in the group.
3. New features asked for meanwhile wait for the next number. Note them, draft the reply, start them only when
   the beta is released.
4. Found good: take `(beta)` off its line, push. The same number is now the stable: boards on the stable get
   their NEW VERSION note, boards on that beta are already on it.
5. Something went wrong: the stable is one flash away for every beta board, and the beta can be replaced by a
   higher one.

Downgrades are told honestly in CHANGES.md: what a board that goes back to the stable keeps and what it loses
(`nvs-settings.md`).

## A contribution while a beta is out

An outside pull request that opens CHANGES.md with a new number goes out as that number when it is merged: a
stable version for every board. When the beta already had that number, the beta moves up instead. Met once: a
PR that added a board came in as `## 0.15` while 0.15 was the beta's number; it was merged as the stable 0.15,
and the beta became 0.16. Check the number of every PR against the site's stable and beta before merging.
