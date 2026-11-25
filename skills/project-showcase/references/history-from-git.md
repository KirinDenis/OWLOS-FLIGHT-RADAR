# The history, from git

Where the history window's data comes from, and the choices made about it. Taken from OWLOS Flight Radar, 2026-11-24
and 25: `.github/history.sh`, `.github/workflows/pages.yml`, `CHANGES.md`, `site/history.js`, `site/growth.js`.

## Three files, all already on the site

The window reads only what the site has anyway, so a new version draws itself with nothing written twice [built,
measured on a test copy of the site]:

- `CHANGES.md`: the versions, newest first. Under each `## x.y` heading a plain line says the version in a few
  words, then its points. The site already shows the file as the history of changes.
- `fw/versions.json`: the firmware the site keeps, version by version, with the boards each has. The flasher reads
  it already. A version the site keeps no firmware of (0.7 here) is taken to have had the boards of the one before.
- `history.txt`: the commits, made by the site's workflow from git. The only new file.

## The summary line under each heading

```markdown
## 0.16

WEATHER, the fourth look, your own units, a screen that dims at night, planes that glide, a clearer map.

- **WEATHER, a fourth look** ...
```

- One plain line, no bullet, right under the heading. The dial and the menu show it as the version's headline.
- Without one, the first point is cut at its first full stop or colon (`headOf()` in history.js) [built].
- The older page that reads CHANGES.md ignores the line: it reads only headings and bullets [measured].
- The file's own intro says what the line is for, so a contributor writes one: "The line under each heading says
  the version in a few words, for the dial of versions on the site."
- Written for every old version once (0.2 to 0.16), from their points, in the same voice.

## history.sh

`files/history.sh` is the script. The workflow runs it after copying CHANGES.md, and a failure does not stop the site:

```bash
bash .github/history.sh > public/history.txt || { echo "no history.txt this time" >&2; rm -f public/history.txt; }
```

It needs the whole history: `actions/checkout` with `fetch-depth: 0`.

The format, one field a tab, the oldest commit first:

```text
C <unix time> <version> <author> <subject>   a commit. version: CHANGES.md's first "## " heading at that commit,
                                             with a b after a beta's number (0.16.1b), - where there was none
A|M|D <path>                                 a file added, changed, deleted
R <old path> <new path>                      a file moved
N <lines added> <lines taken out> <path>     how much changed: - and - for a picture or another binary
```

- `git log --reverse --no-merges -M --name-status` gives the files; `git show --numstat -M` per commit the line counts.
  Moves are followed (`-M`), so a renamed file keeps its thread.
- **The author's name only, never the email.** The format string has `%an`, not `%ae`. The history is public, and an
  address in it is an address handed to every scraper [decision].
- The version is read from CHANGES.md as it was at that commit (`git show <sha>:CHANGES.md`), so each commit carries
  the version it was written towards.
- For the radar on 2026-11-25: 49 commits (the maintainer squashes), 62 KB of text, 8 s in Git Bash on the Windows
  build PC [measured]. In CI it was not timed.
- Run it in a clone, never by switching the branch of a working tree that holds someone's uncommitted work.

## A version only steps forward

Branches break a naive "version of each commit". The Tab5 work was on a side branch whose CHANGES.md said 0.8 while
main was still at 0.6; sorted by time the commits read 0.6, 0.8, 0.7, 0.8 [measured on the radar's log].

`versionSteps()` in growth.js (exported, the tower uses it too) walks the commits in time order and moves the
version only to the next one that is present, and only when this commit has it, or has passed it without that
version appearing again later. A commit from a side branch that is ahead takes the version of its time:

```js
export function versionSteps(commits, versionIndex) {
  const raw = commits.map((c) => versionIndex(c.ver));
  const present = [...new Set(raw.filter((v) => v >= 0))].sort((a, b) => a - b);
  let cur = -1;
  commits.forEach((c, i) => {
    const next = present.find((v) => v > cur);
    if (next !== undefined && (raw[i] === next || (raw[i] > next && !raw.slice(i).includes(next)))) {
      cur = raw[i];
    }
    c.vi = cur;
  });
}
```

The version's index comes from the menu's list, which is CHANGES.md's headings. A beta's commits say `0.16.1b`; the b
is dropped and the number is matched to the heading it starts with, so the 0.16.1 and 0.16.2 betas, merged into one
stable `## 0.16`, count as 0.16 [built, and the radar's log reads so]. A commit from before CHANGES.md existed counts
as the first version.

## The parts of the product

The tower's floor and the menu's PARTS chips cut the repository by what a person would call its parts, not by
folders alone. The first match wins:

- Screen: `core/` (the drawing shared by firmware, PC and browser)
- Boards: `firmware/`
- Site: `site/` and `web/`
- Server: `server/`
- Tools: `tools/`
- Pictures: `shots/`
- Texts and build: everything else (README, CHANGES, `.github`)

Pick the parts for another project the same way: five to seven, each a word a visitor understands, one colour each,
checked for colour-blind eyes, and the part's name said in words wherever its colour is shown.
