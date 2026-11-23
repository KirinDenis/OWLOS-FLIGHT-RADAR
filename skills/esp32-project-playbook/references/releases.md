# Releases: beta, then stable

How a version of an ESP32 project here goes from the working tree to people's boards. Taken from OWLOS Flight Radar
(its `CHANGES.md`, `README.md`, `.github/workflows/pages.yml`, `.github/archive.sh`) and the work around it. The
build and flash mechanics are in esp32-flash-and-settings; the firmware side of the version is in esp32-runtime.
How sure: [measured] seen happen here, [built] in the code but not seen, [docs] read, [reasoned] worked out.

## One number in one place

- The first `## ` line of `CHANGES.md` is the version. The firmware is built with it, the PC preview and the web
  twin show it, CI publishes it in `version.json`, and the site shows the whole file as the history.
- A new number keeps the last version on the site for good; the same number replaces it. Bump before pushing
  anything that changes firmware, unless the maintainer says a fix goes in under the same number.
- A board compares its own number with the site's and shows NEW VERSION. A stable board reads only `version` from
  `version.json`: boards older than the beta channel read nothing else, and only digits and dots, so that field must
  stay a plain number for good [built].

## The beta channel

- `(beta)` on the first line makes it a beta: `## 0.16.1 (beta)`. The site keeps the beta beside the stable: the
  beta in `fw/<version>/<board>/`, the stable in `fw/<board>/`, and the stable is still what the site flashes unless
  the beta is picked.
- `version.json` says both: `version` the stable, which every board compares with; `beta` the beta, which only a
  beta build follows. A board on the stable never hears of a beta by itself.
- A beta needs a number above the stable. A new beta replaces the last one. The next stable version at the top of
  the file ends it. A beta found good becomes the stable by taking `(beta)` off its line, nothing else.
- A board new in the beta has no stable build yet: the site flashes it the beta with no choice to make, and it
  follows the betas till the stable has it too. The site keeps a list of the boards in the stable for this.
- Back to the stable is the same row on the site, STABLE, then flash; settings stay.
- Beta steps get their own numbers (0.16.1, 0.16.2): each is a build people may hold, and each is kept on the site.
- **The channels go in first.** CI, the archive script and the site must know beta from stable before the first
  push of a beta, or that push reaches every board as a plain update [reasoned, and so it was planned and done].

## Holding a beta

- A beta is held for a few days on real boards before it becomes the stable. While it is held it is **frozen for
  features**: only fixes go in. Whether a fix gets a new number is the maintainer's call; a fix under the same number
  replaces that build, and boards that took the first one get no note.
- A new ask that comes while a beta is held is written down for the next version, and only the reply is drafted.
- A feature promised in an issue ("in a coming beta") goes on the next version's list the same day.
- Before a beta goes out at all, one bench test of all of it together (see the bench rigs in esp32-runtime's
  testing reference): fresh builds with CI's command line, the site assembled by the workflow's steps, flashing from
  the manifests, settings compared by field name, a round trip stable -> beta -> stable -> beta, a soak. A beta that
  goes out "full" once beats several early pushes.
- New views and options are off by default, so a beta's memory comparison is one build with the switch, not two
  builds.

## The archive of versions

GitHub releases are not used. The workflow's token was not allowed to create a tag in the repository (`403
Resource not accessible by integration`), not on the built commit and not on an empty commit of its own; two runs
failed so and created nothing [measured]. So the site itself keeps each version:

- `fw/<board>/` the newest stable, `fw/<version>/<board>/` every older version and the beta, all listed in
  `fw/versions.json`.
- Each run takes the older versions from the live site as it is and puts its own build next to them; nothing is
  built again. Every build of a version replaces that version, the older ones are never touched: to keep a build,
  give the next one a new number.
- A lower number than one the site has, or a file of an older version the run cannot fetch, stops the workflow
  before deploy, and the site stays as it was.
- Each run also keeps all of it as a `site-fw` artifact for 90 days, a second copy.
- The versions from before the archive existed came once from the CI runs that built them, while their artifacts
  lasted (`.github/archive-seed.txt`).
- The page offers older versions with a select and `?v=0.3`.

## A board added means a list of places

A new build touches every one of these; a missing one is a board that builds and cannot be flashed, or is not
counted:

- `core/config.h` (its number), `firmware/main/board.h` (its block), `firmware/BUILD.BAT` (its name), and its
  sdkconfig file in `firmware/CMakeLists.txt` if it needs one.
- The CI matrix in `pages.yml`, `.github/make-manifests.sh`, the local `tools/assemble.sh`.
- The site: its card, the list of boards in the stable, any board-specific note (a board with no BOOT key, one
  whose touch firmware lives in NVS and must not be erased).
- The web twin if it needs a layout of its own.
- The stats server's list of builds, which it reads once at start: a board added to the repo means a server update
  too [measured: a list fell behind by four boards].
- `README.md` (the boards list, with "New: not run on one here yet" and where the pins came from), `CHANGES.md`,
  `BOARDS-WANTED.md`.

## Publishing, in order

1. `CHANGES.md`: a new `## x.y` section, `(beta)` if it is one.
2. Build every board the change touches locally; read the warnings count and the sizes. A rebuild of an untouched
   board should keep its size: a change in size is a question. Check the smallest layout's room left.
3. Flash at least one real board as a regression test, even when the change is for a board you do not have.
4. Assemble the site locally and check it: every card, every manifest, every part, the console, the version
   picker, an old board and a beta board against a fake port.
5. The maintainer reviews, commits and pushes. The agent never does.
6. Check the run from outside, no login needed:

```powershell
$h = @{ "User-Agent" = "project-check" }
$r = Invoke-RestMethod "https://api.github.com/repos/<owner>/<repo>/actions/runs?per_page=3" -Headers $h
$j = Invoke-RestMethod "https://api.github.com/repos/<owner>/<repo>/actions/runs/$($r.workflow_runs[0].id)/jobs" -Headers $h
$j.jobs | ForEach-Object { "$($_.name): $($_.conclusion)" }
```

   Job logs are not public to the API (403): for the text of a failed step, ask the maintainer for those lines and
   say exactly which. Then fetch the live `version.json`, `fw/versions.json` and one old file with curl, and let a
   board on the bench say what it heard (`HTTP 200, <version>` in its log).
7. Only then the group post. A post about boards whose cards are not live yet sends people to nothing.

## CI on pull requests

- The workflow builds every board on a pull request into main and publishes nothing: the site job is skipped on
  `pull_request`, and the concurrency group is per ref so a PR does not cancel a deploy.
- A pull request from a fork waits for the maintainer to approve its CI run.
- The branch rules ask for an approval and for the last push to be approved by someone else; a maintainer working
  alone merges through the rules' bypass list.

## When CI fails on something you can't reproduce

Read the REST docs for that endpoint's permissions before guessing. Two pushes went on a tag the token was never
allowed to make. A fake proves your logic, not GitHub's rules. After a failed guess prefer the design that needs no
permission at all: that is how the archive came to live on the site.
