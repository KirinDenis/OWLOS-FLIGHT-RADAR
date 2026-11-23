# The archive of old versions

Every published version stays flashable from the site, so a person whose board went wrong on the newest can go
back with the same button. Real files, whole: `../files/flight-radar/archive.sh` and `archive-seed.txt` (both go
to `.github/`), the workflow in `../files/flight-radar/pages.yml`. Source repo: OWLOS Flight Radar. FusionEdge
runs a variant of the same script (end of this file).

How sure: on the real pipeline. Run 6 (commit bc203cf) made the live site with 0.4 in `fw/`, 0.3 and 0.2 in
`fw/0.3/`, `fw/0.2/`, the two old ones taken from their old runs. On 2026-11-23 the live list held 13 versions,
0.2 to 0.15, and the beta 0.16.2 beside them; `check-site.sh` fetched all 586 files, 0 bad (measured). The
scenarios below were run locally against fakes, once read-only against the real site.

## The story: GitHub Releases failed twice

The first design was the obvious one: a GitHub release a version, tag `v0.4`, a zip a board in it, the site job
downloading older releases. It looked right, it passed every local test on a fake `gh`, and it failed on GitHub:

- **Run 4** (8f39ff7): job "Keep this version", step "The versions from before the archive, once": HTTP 403
  "Resource not accessible by integration" on POST /releases. The job had `contents: write`.
- The REST docs, Create a release, say it: when the target commit adds or modifies anything under
  `.github/workflows/` relative to the default branch, the token must be authorized to modify workflows, and
  **the GITHUB_TOKEN cannot be**. They name 404 as the answer and 403 for some paths. Each commit we wanted to tag
  changed the workflow, because each version did.
- **Run 5** (525d116): a way round. An own empty commit, same tree as the built commit, the built commit as its
  parent, made with `gh api repos/$GH_REPO/git/commits`, and the tag on that. The commit got created. Its tag was
  refused the same way. A likely reason, from the docs' words, not tested: "relative to the default branch"
  compares with main, not with the parent, and an old version's tree has an old workflow file.
- No release and no tag was made in either run, and the old site stayed live.

A PAT or a GitHub App token with the Workflows permission should get through, by the docs (not tried). That is a
secret to keep in every fork, so the design moved instead: **the archive lives on the site itself**. Pages deploys
what you give it, with no tag and no extra permission. The site is both the store and the shop window.

Lesson: **a fake proves your logic, not GitHub's rules. Read the REST docs for the permission first.**

## The design

```
fw/<board>/...                  the newest stable: bootloader.bin partition-table.bin <app>.bin flash_args manifest.json
fw/<version>/<board>/...        each older one, same files; and the beta, when there is one
fw/versions.json                all of them, the newest first, the beta apart
```

Real `fw/versions.json` from the live site on 2026-11-04 (now 13 versions and a beta, `beta-and-stable.md`):

```json
{
  "versions": [
    { "version": "0.4", "text": "0.4, build 6 (bc203cf)", "dir": "fw", "boards": [ "crowadv28", "es3c28p", "gc9a01", "ili9341", "ili9486", "nmcydc5", "st7796" ] },
    { "version": "0.3", "text": "0.3, build 3 (9a2c152)", "dir": "fw/0.3", "boards": [ "gc9a01", "ili9341", "ili9486", "st7796" ] },
    { "version": "0.2", "text": "0.2, build 1 (0a86826)", "dir": "fw/0.2", "boards": [ "gc9a01", "ili9341", "ili9486", "st7796" ] }
  ]
}
```

One version a line, on purpose: the script reads it back with `sed`, there is no jq on every machine.

Rule: **a build of the same number replaces it, a new number keeps the old one.** Every push rebuilds; a doc fix
must not add a version. To keep a build, give the next one a new number in `CHANGES.md`. Nothing is built twice: a
version once moved to `fw/<version>/` is copied from the live site byte for byte for ever.

A number that no run published is simply not in the list: 0.7 is missing from the live one. Its commit came 40
minutes before 0.8's: either it was never pushed alone, or its run was cancelled by the next push (reasoned:
`concurrency` cancels the older run of the same branch). To keep a version on the site, let its run finish before
pushing the next number.

Page deploys replace the whole site. So each run has to carry every older version forward itself, and that is
what `archive.sh` does.

## archive.sh, block by block

Called from "Put the page together", after `make-manifests.sh` made this build's manifests:

```bash
bash .github/archive.sh public/fw "$VER" "$VERSION" "$SITE" .github/archive-seed.txt "$BETA"
```

`$SITE` is `steps.pages.outputs.base_url`. Needs curl, and gh only while a seed is still to be fetched.

1. **Read the live list.** `curl` of `$URL/fw/versions.json?run=$GITHUB_RUN_ID`, with `--retry 4 --retry-delay 5`
   (Pages answers 503 now and then):
   - 200: `sed` into lines `version|text|dir|board board ...`, the beta's line left out and read apart.
   - **404: none yet**, the empty list. Only a site that never kept versions answers this.
   - anything else: stop. A site that is down must not look like an empty one, or the versions would go.
   - `?run=<id>`: GitHub Pages' CDN answers `Cache-Control: max-age=600` (measured). A push ten minutes after the
     last one would read the old list and old files. A new query string is a new cache key.
2. **Fetch each older version.** For each line, `newer "$v" "$VER"` (with `sort -V`) stops the run when the live
   site has a higher number than `CHANGES.md`: "a new version needs a higher number". The same number is skipped:
   this build is that version now. Any other version: every board, every file in `FILES`, each with `?run=` and
   the retries. **Any answer but 200 stops the job before deploy**, the site stays as it was. When 0.5 comes, the
   live `dir: fw` version 0.4 lands in `fw/0.4/` this way: that is the move. In a beta's run the stable is fetched
   back into `fw/` instead (`beta-and-stable.md`).
3. **The site's beta**: a beta run with a lower number than the site's beta stops; any other beta run replaces
   it; a stable run ends it. Its files are not kept.
4. **Seeds: versions from before the site kept them, once.** For each line of `archive-seed.txt` not already kept:
   `gh run download "$run" -p 'fw-*' -D "$tmp"`, then each `flash_args` found gives a board folder. Needs
   `actions: read` and `GH_TOKEN`, `GH_REPO`. Artifacts last 90 days (0.2's and 0.3's expire 2027-01-31, by the
   public API), and once the site has a version the line is passed over and gh is not called at all.
5. **Write the list.** `entry()` prints one JSON line a version, boards from the `manifest.json`s present. Older
   versions get `make-manifests.sh "$FW/$v" "$text"`, sorted `sort -t '|' -k 1,1 -V -r`. All the text is ours
   (CHANGES.md, git log, the script's names): nothing to escape. The beta goes last, on its own line.

Details that are there for a reason:

- `while ... <&3 ... done 3< file`: gh and curl inside the loop must not eat the list on stdin.
- `FILES` names what a version is. `flash_args` is kept on the site for this: a manifest can be made again from
  it. If you add a merged image, add it here.
- Set your own app file name in `FILES` and in `make-manifests.sh`.

The seed file:

```
# a line: the version, the run's id, the version as the page will say it
0.2 41421201551 0.2, build 1 (0a86826)
0.3 41636456462 0.3, build 3 (9a2c152)
```

A new repo starts with no seeds, an empty file with the comment, and `actions: read` can go. The first run gets
404 and writes one version.

**The second copy**: an upload of `public/fw` as artifact `site-fw`, 90 days, every run. The live site is the only
long-term store, so a hole remains: a Pages site taken down on purpose answers 404, which reads as none yet, and
the next run starts over from the seeds. Then restore from the newest `site-fw`, or put the files back with a seed
line per version.

**Size**: a version with 7 boards was about 8 MB; the whole archive was about 170 MB at 13 versions and up to 17
boards (measured when it was mirrored for a rehearsal). A Pages site may be 1 GB (read from docs), and every run
fetches the whole archive and deploys it again. Watch the size as boards are added: a project with many firmwares
will want its own store some day.

## The page

The old page (now at `site/previous/`): step 1, above the cards, hidden until there are two versions:

```html
<div class="older" id="older" hidden>
  <label>The buttons below flash: <select id="pick"></select></label>
  <p class="nope" id="older-note" hidden></p>
</div>
```

```js
const flashers = [...document.querySelectorAll('esp-web-install-button')].map(b => {
  const m = (b.getAttribute('manifest') || '').match(/^fw\/([^/]+)\/manifest\.json$/);
  if (m) {
    b.setAttribute('manifest', onSite(m[0]));       // the whole address: the page sits under a <base>
  }
  const later = document.createElement('p');
  later.className = 'nope flash';
  later.hidden = true;
  b.after(later);
  return { b, board: m ? m[1] : '', later };
});

function pickVersion(all, i) {
  const v = all[i];
  for (const f of flashers) {
    if (!f.board) {
      continue;
    }
    const has = v.boards.includes(f.board);
    f.b.setAttribute('manifest', onSite(v.dir + '/' + f.board + '/manifest.json'));
    f.b.hidden = !has;
    f.later.hidden = has;
    f.later.textContent = has ? '' : 'Not in ' + v.version + ': this board came in a later version.';
  }
  const note = $('older-note');
  note.hidden = i === 0;
  if (i === 0) {
    note.replaceChildren();
    return;
  }
  // (the real text links to the group)
  note.replaceChildren(v.version + ' is an older version, for a board the newest went wrong on. ' +
    'An older version may not read all the settings a newer one saved: the WiFi stays, the rest may need step 2 again.');
}

fetch('fw/versions.json', { cache: 'no-cache' }).then(r => r.ok ? r.json() : null).then(j => {
  const all = j && Array.isArray(j.versions) ? j.versions.filter(v => v && /^[0-9]+(\.[0-9]+)*$/.test(v.version) &&
    /^fw(\/[0-9.]+)?$/.test(v.dir) && Array.isArray(v.boards)) : [];
  if (all.length < 2) {
    return;
  }
  const pick = $('pick');
  all.forEach((v, i) => {
    const o = document.createElement('option');
    o.value = String(i);
    o.textContent = v.version + (i === 0 ? ', the newest' : '');
    pick.append(o);
  });
  pick.addEventListener('change', () => pickVersion(all, Number(pick.value)));
  $('older').hidden = false;
  const asked = all.findIndex(v => v.version === new URLSearchParams(location.search).get('v'));
  if (asked > 0) {
    pick.value = String(asked);
    pickVersion(all, asked);
  }
}).catch(() => {});
```

- `?v=0.3` picks a version from a link: "try the older one" in one message.
- A board missing in that version says so instead of showing a button: "Not in 0.3: this board came in a later
  version."
- It works because ESP Web Tools reads the attribute at the click (`web-flasher.md`).
- `onSite()` is `new URL(path, document.baseURI).href`: ESP Web Tools reads a manifest against the page's own
  address, not a `<base>`.
- The list is checked before use: versions of digits and dots, folders `fw` or `fw/<digits and dots>`.
- The note about settings is honest, see below.

The new page (0.8 on) offers the older versions through the previous page ("Looking for the page you know, with
every firmware version and the full guide? Previous page"), and the stable or the beta itself.

## Older firmware and newer settings

0.2 on a board that ran 0.4: `nvs_get_blob` with 0.2's smaller size on a larger stored blob gets
`ESP_ERR_NVS_INVALID_LENGTH`, `load_blob` says no, 0.2 takes its defaults. Strings stand apart as their own NVS
keys, so the WiFi, the password and the API key stay. From the code path; see `nvs-settings.md`. The newer
version, flashed again, reads whatever the older one saved by its migration chain.

## How it was tested with no gh, jq, zip, Docker or WSL on the PC

Windows, Git Bash, curl, nothing else. So each missing tool got a stand-in, and the script ran for real:

- **A fake gh** on PATH (`../files/rigs/fake-gh.sh`) keeping its state in folders: `$FAKE/runs/<id>/fw-<board>/`.
  The release design's version also faked `release create/view/list/download/delete`, `api`, `run view` with
  `$FAKE/releases/<tag>/`.
- **Windows' own `tar.exe` as zip**, for the releases design: `tar.exe -a -cf out.zip files` writes a zip.
- **A local http server as the fake live site**: a folder served with `npx http-server -c-1`, a site one run older
  each time. `curl` needs real HTTP codes, `file://` will not do.

Each scenario was one run of `archive.sh` in a folder with a `fw/` as the firmware job leaves it:

1. **First run**: live answers 404, seeds fetched. 0.4 in `fw/`, 0.3 and 0.2 from the fake runs, list of three.
2. **Rebuild**: 0.4 again as build 7, the site serves run 1's output. `text` updates, 0.3 and 0.2 come from the
   site, not from the seeds.
3. **Bump**: `CHANGES.md` says 0.5. 0.4 appears in `fw/0.4/`, the old newest **moved byte for byte**.
4. **Lower number**: 0.3 against a site with 0.4. Stops with "a new version needs a higher number", nothing
   written.
5. **Missing file**: one file removed from the fake site. Stops with its 404 and "or 0.3 would be lost".
6. **Site down**: server stopped. curl fails, `set -e` stops the job before deploy.

Then once read-only against the real site: it reads, and only a deploy would write. For the 0.16.1 rehearsal the
live list was mirrored with stub files (`ci-and-versions.md`).

What this could not prove: permissions, CDN caching, what `configure-pages` outputs, artifact names after
`merge-multiple`. Those came from the docs and from run 6. The fake passed the releases design too.

## FusionEdge's variant

The same script, cut for a PlatformIO project with no seeds:

- The version is `FW_VERSION` in `src/core/options.h`: "a new version needs a higher FW_VERSION".
- A version is whatever its `flash_args` names, so the script fetches `flash_args` first, then each part it
  lists, then the board's zipped one-file image. A `get()` helper does each fetch with the retries and `?run=`.
- No `actions: read`, no `gh`: the site kept versions from its first run.

## To reuse in a new repo

1. Copy `archive.sh`, an empty `archive-seed.txt`, `make-manifests.sh`.
2. Set `FILES` and the board `case`.
3. In `pages.yml`: `configure-pages` before the assembling step, `SITE: ${{ steps.pages.outputs.base_url }}`, the
   `site-fw` upload. `actions: read`, `GH_TOKEN`, `GH_REPO` only if there are seeds.
4. On the page: the `older` block and the script above. Its regex in `flashers` expects
   `manifest="fw/<board>/manifest.json"`.
5. The version line in `CHANGES.md`. Then `check-site.sh https://you.github.io/repo` after the first deploy.
