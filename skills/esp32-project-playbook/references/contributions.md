# Contributions and pull requests from people

How work from other people came into OWLOS Flight Radar: a pull request adding one board, and a fork that added
three boards and a new way of drawing. The technique only. Credit goes by public GitHub handle, as the public
repository already gives it (here: `bertvm`, `chipguyhere`, `parispitts`), never by email.

## Review by what reaches the boards the author does not have

A contributor tests on their own board. The review's first question is what the change does to every other board.
List it before anything else:

- Code that compiles for every board, not only theirs: a connection kept alive between requests, a WiFi scan
  change, a log buffer on every PSRAM board, scrolling that flings, a different line drawing at some scales,
  diagnostics with a TEMP mark, a console parsing change, a component pinned to an older version.
- Settings files shared by every board (`sdkconfig.defaults`).
- Build files: a second, root `CMakeLists.txt` that duplicates the firmware's and defaults to the author's board.

For each item, one of three:

- **Keep for all boards**, when it is a general fix and an A/B on our own boards shows no harm (joining the strongest
  access point of a network; finding the console's command anywhere in a line).
- **Gate it** under the contributor's own build switch, with the original code in `#else`, so the other boards
  keep exactly what they ran (the kept-alive connection, the log buffer, the timing diagnostics, fling scrolling,
  the scaled lines).
- **Take it out before merging**, saying why, in the review:
  - code written while the author's board was misidentified, for a board the author does not have;
  - a gate that makes every HTTPS request wait for NTP: a network that blocks NTP would never fetch, and with
    certificate dates unchecked there is no need;
  - a root `CMakeLists.txt` that duplicates the firmware's;
  - changes that belong to another IDF major version (pin hold and sleep changes for every I2C board under IDF 6).
  Guards that let the code build on IDF 6 and do nothing on 5.5 can stay.

After a big merge: a whole-code review by several read-only agents, each with one concern (memory and leaks, board
pins, correctness), the fixes in without a version change, every board rebuilt, then the bench. It found a clock
wrap at 24.8 days, a crash path on the 3.5" and faults in the tile handling; the bench after it found the cause of
a tester's map holes on the 3.5" [measured].

## A fork with history you do not want

The fork's history carried a vendor's GPL-2.0 touch firmware (removed in a later commit) and 7.4 MB of video.
A plain merge keeping its history was refused: it would bring both into the project's history for good. Instead:

- `git merge --squash` of the fork's last commit onto a branch of the project's own, conflicts resolved there.
- The video left out and linked to in the contributor's repository.
- The GPL firmware never in the tree: the contributor's own web installer puts it in the board's NVS, and the
  project's site warns not to erase that board.
- The contributor's copyright kept: a line in the root `LICENSE` with name and handle, and a header line on each
  of their new files. Their vendored libraries keep their own licence files.
- Author credit on the squashed commit (`--author`), or a `Co-authored-by:` trailer at the merge to main. The
  maintainer commits; the agent prepares the tree.
- Markers like "fork addition" in the code replaced by the handle.
- The fork's board numbers kept, so the contributor's later changes come over clean; the project's next board took
  the next free number.
- The contributor's notes kept as their own file (`FORK-NOTES.md`), its top saying whose they are.
- The integration work is the project's: board registration in CI and the site, README with credit and a link to
  the fork, CHANGES, gating, testing. The contributor is not asked to do it.

## A pull request redone clean

When the review asked for parts to come out, the contributor opened a new, clean pull request. The maintainer's
plan, which worked:

1. Approve its CI run (a fork's run waits for that), merge it as it is, pull.
2. Then the follow-up, by the maintainer's hand or reviewed by them: the board marked "New: not run on one here yet"
   in the README, the site and the manifest (it was never run here; its pins came from the maker's firmware); the
   handle credited in README and CHANGES; a `LICENSE` copyright line with no email; README lines the change made
   stale; a CHANGES line saying which boards need not update.
3. Rebuild under the same number, check CI is green on the PR and on both pushes, redeploy anything that keeps a
   list of builds (the stats server).

Nothing is edited locally between the merge and the maintainer's pull: a dirty tree blocks the pull.

**Versions.** The clean PR's `CHANGES.md` opened a new version, which would reach every board as an update while a
beta was out. The maintainer merged it as the next stable and renumbered the beta above it (0.15 the merge, 0.16
the beta). Decide this before merging, not after.

## Working in someone else's project

The same rules the other way round, from a contribution to FusionEdge, an internet radio:

- A fork of their repository, and the work in a clone the maintainer can look at.
- Changes for new boards under build switches in that board's own config, so their existing builds stay the same
  code. Prove it: build before and after and diff the objects with addresses masked.
- Their licence and ours: never bring their GPL code into the project's MIT repositories. The other way is allowed:
  our MIT renderer and fonts went into theirs, with the MIT notice kept [reasoned, from the licences].
- Squash junk out of the fork's history (an IDE's database file) before a pull request upstream.
- What they asked for, nothing wider. A screen of ours on their other boards was built, then dropped when the
  maintainer said it was not needed: a switch on the site nobody wanted is a cost.

## Credit, in one place

- Public handle, as the person shows it in public. No email in any file.
- `LICENSE`: a copyright line per contributor of code.
- `README.md`: the boards they brought, with their handle and a link to their repository.
- `CHANGES.md`: the version that took their work, with the handle.
- An idea from feedback that was built: the handle where the change is described, if they posted in public.
