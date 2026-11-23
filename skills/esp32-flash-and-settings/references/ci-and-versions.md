# CI, Pages, manifests, versions

Real files: `../files/flight-radar/pages.yml` (goes to `.github/workflows/pages.yml`), `make-manifests.sh`
(`.github/`), `CMakeLists.txt` (`firmware/`), `gitattributes.txt` (`.gitattributes`). Source repo: OWLOS Flight
Radar. The beta channel the workflow carries: `beta-and-stable.md`. The archive of older versions: `archive.md`.
A PlatformIO project's variant (FusionEdge): `platformio-pipeline.md`. Files for esptool users: `bin-files.md`.

How sure: this workflow runs on GitHub on every push. Measured on the live site on 2026-11-23: the stable 0.15
(build 46) with 16 boards, 13 versions kept from 0.2 on, and the beta 0.16.2 (build 51) with 17 boards;
`check-site.sh` fetched 586 files, 0 bad. The same shape runs in OWLOS WiFi Radar and FusionEdge. ESP32 Web
Flasher's workflow has NOT run on GitHub yet.

## The shape

```
push to main (or the Run workflow button)
  job firmware (matrix: a job a board, in parallel, in Espressif's docker image)
      -> artifact fw-<board>:  bootloader.bin partition-table.bin <app>.bin flash_args
      -> artifact elf-<board>: <app>.elf sdkconfig   (90 days, for crash backtraces)
  job web (the radar in the browser: core/ for WebAssembly, emsdk's docker image)
      -> artifact web: index.html twin*.js twin*.wasm
  job site (needs both, runs even if a board failed, never on a pull request)
      download fw-* -> public/fw/<board>/, web -> public/radar/
      site/ + CHANGES.md + shots/ -> public/
      manifests, a beta beside the stable, older versions, fw/versions.json, version.json, @VERSION@ -> public/
      artifact site-fw (every version, second copy), upload-pages-artifact, deploy-pages
pull request into main
  firmware and web as above, nothing published: the firmware stays with the run as its artifacts
```

Needs once: repo **Settings, Pages, Source: GitHub Actions**. Without it deploy fails with "Get Pages site
failed". A public repo: Pages for private ones is a paid plan.

## pages.yml, the lines that matter

Top of the file:

```yaml
on:
  push:
    branches: [ main ]
  # a pull request into main: every board and the radar page are built, nothing is published
  pull_request:
    branches: [ main ]
  workflow_dispatch:

# each job asks for what it needs on top of this
permissions:
  contents: read

# a push to main cancels the run of main before it, a pull request's push the run of that request:
# a pull request never stops the site being published
concurrency:
  group: pages-${{ github.ref }}
  cancel-in-progress: true
```

- `workflow_dispatch`: the Run workflow button, for a rebuild with no commit.
- `pull_request`: a contributor's PR builds every board on GitHub before it is merged (measured: the PR that
  brought the plain CrowPanel 5.0 was green on every board before the merge). The site job is skipped for it
  (below).
- Least privilege per job. **A job's `permissions:` replaces the top one, it does not add to it**, so the site
  job writes `contents: read` again. firmware needs only read. site needs `pages: write`, `id-token: write`
  (deploy-pages proves itself with OIDC) and `actions: read` (only for `gh run download` of the seeds, see
  `archive.md`).
- `concurrency` by ref with cancel: two quick pushes publish only the last; a PR's run never cancels main's. A
  cancelled run's site job does not run, because of `!cancelled()`.

The firmware job:

```yaml
strategy:
  fail-fast: false
  matrix:
    include:
      - { disp: ili9341, num: 1, target: esp32 }
      - { disp: gc9a01,  num: 4, target: esp32c3 }
      - { disp: es3c28p, num: 7, target: esp32s3 }
      - { disp: nmcydc5, num: 8, target: esp32c5 }
      - { disp: tab5, num: 12, target: esp32p4 }
      # ... 17 boards in all; the e-paper boards are not on the site yet
steps:
  - uses: actions/checkout@v4
  - name: Build
    # the whole repo goes in, firmware/ pulls ../core. git inside the container runs as
    # root on files that aren't root's, hence the safe.directory line
    run: |
      docker run --rm -v "${{ github.workspace }}:/project" -w /project/firmware --user root \
        espressif/idf:release-v5.5 \
        bash -c "git config --global --add safe.directory '*' && \
                 idf.py --no-ccache -B build_${{ matrix.disp }} \
                   -DSDKCONFIG=/project/firmware/build_${{ matrix.disp }}/sdkconfig \
                   -DIDF_TARGET=${{ matrix.target }} -DRADAR_DISPLAY=${{ matrix.num }} build"
```

- `fail-fast: false`: one board that does not compile must not take the others down.
- `docker run` rather than `container:` or the esp-idf action: the same image and line as the local `idf.py`,
  nothing else in between.
- `release-v5.5` is a moving branch image, a later 5.5 than the local tag. The C5 is a normal target there, a
  preview on the 5.5 tag (`local-flash.md`), and the P4 JC8012 configures there and not on the local tag. A
  local build and the site's build are not the same bytes, and a local ELF does not match a site-flashed board.
- The board's folder and sdkconfig match BUILD.BAT, so the same defaults apply.

Collect and upload:

```yaml
- name: Collect the binaries
  # flash_args is written by the build: where each part goes
  run: |
    B=firmware/build_${{ matrix.disp }}
    mkdir -p out/${{ matrix.disp }}
    cp "$B/bootloader/bootloader.bin" "$B/partition_table/partition-table.bin" \
       "$B/owlos-flight-radar.bin" "$B/flash_args" out/${{ matrix.disp }}/
- uses: actions/upload-artifact@v4
  with:
    name: fw-${{ matrix.disp }}
    path: out
# not for the site: the ELF turns a crash backtrace from a board flashed off the
# site into file and line. a locally built ELF doesn't match
- uses: actions/upload-artifact@v4
  with:
    name: elf-${{ matrix.disp }}
    path: |
      firmware/build_${{ matrix.disp }}/owlos-flight-radar.elf
      firmware/build_${{ matrix.disp }}/sdkconfig
    retention-days: 90
```

The artifact holds `<board>/...` because `path: out` keeps the folder under it. Measured sizes: `fw-*` about
780 KB, `elf-*` about 4.3 MB.

The web job builds the radar's screen code for the page, from the same files as the firmware
(`fake-board-testing.md` has what it is for):

```yaml
web:
  steps:
    - uses: actions/checkout@v4
    - name: Build
      run: |
        VER=$(sed -n 's/^## \([0-9][0-9.]*\).*/\1/p' CHANGES.md | head -n 1)
        # each with its own letters: the 5" has its own set, in the glass's pixels
        FONTS="../core/font_small.c ../core/font_medium.c ../core/font_large.c ../core/font_huge.c"
        GLASS="../core/font_glass_small.c ../core/font_glass_medium.c ../core/font_glass_large.c ../core/font_glass_huge.c ../core/font_glass_tiny.c"
        for one in 10:Twin:twin 1:Twin1:twin1 2:Twin2:twin2 4:Twin4:twin4 13:Twin5:twin5; do
          IFS=: read -r display name file <<< "$one"
          letters=$FONTS
          [ "$display" = 10 ] && letters=$GLASS
          docker run --rm -v "${{ github.workspace }}:/project" -w /project/web --user root \
            emscripten/emsdk:6.0.13 emcc @emcc.args $letters "-DFLIGHT_VERSION=\"$VER\"" \
            "-DRADAR_DISPLAY=$display" "-sEXPORT_NAME=$name" -o "$file.js"
        done
    - uses: actions/upload-artifact@v4
      with:
        name: web
        path: |
          web/index.html
          web/twin*.js
          web/twin*.wasm
```

One twin a screen size (800x480, 320x240, 480x320, the round one, 480x272). `web/emcc.args` lists the flags and
the files, and the local `web/BUILD.BAT` reads the same file, so the two cannot drift. emsdk 6.0.13 is the same
locally (measured).

The site job:

```yaml
site:
  needs: [ firmware, web ]
  if: ${{ !cancelled() && github.event_name != 'pull_request' }}   # a failed board: the others go out as before
  permissions:
    contents: read
    pages: write
    id-token: write
    actions: read
  environment:
    name: github-pages
    url: ${{ steps.deploy.outputs.page_url }}
  env:
    GH_TOKEN: ${{ github.token }}
    GH_REPO: ${{ github.repository }}
  steps:
    - uses: actions/checkout@v4
      with:
        fetch-depth: 0          # for the build number
    - uses: actions/download-artifact@v4
      with:
        pattern: fw-*
        path: public/fw
        merge-multiple: true
    - uses: actions/download-artifact@v4
      continue-on-error: true   # no radar page this time: the flasher still goes out
      with:
        name: web
        path: public/radar
    - id: pages                 # base_url: where the site is now
      uses: actions/configure-pages@v5
```

- `fetch-depth: 0`: `git rev-list --count HEAD` on a shallow clone says 1.
- `merge-multiple: true` merges the `fw-*` artifacts into one tree: `public/fw/<board>/`.
- `continue-on-error` on the web artifact: a radar page that did not build must not hold the flasher back.
- `steps.pages.outputs.base_url` is the site's own address, no hard-coded URL, so forks work.

The page is put together, in order:

```bash
HEAD=$(grep -m 1 '^## ' CHANGES.md || true)
VER=$(printf '%s\n' "$HEAD" | sed -n 's/^## \([0-9][0-9.]*\).*/\1/p')
[ -n "$VER" ] || { echo "no version in CHANGES.md: its first '## ' line should be one" >&2; exit 1; }
BETA=0
WORD=""
case "$HEAD" in *'(beta)'*) BETA=1; WORD=' beta' ;; esac
VERSION="$VER$WORD, build $(git rev-list --count HEAD) ($(git rev-parse --short HEAD))"
cp -r site/. public/
cp CHANGES.md public/
STATS=$(tr -d ' \r\n' < server/public-url.txt 2>/dev/null || true)
mkdir -p public/shots && cp -r shots/. public/shots/
if [ "$BETA" = 1 ]; then                 # this run's boards go beside the stable, not over it
  mkdir -p "public/fw/$VER"
  for d in public/fw/*/; do
    if [ -f "$d/flash_args" ]; then
      mv "$d" "public/fw/$VER/$(basename "$d")"
    fi
  done
  bash .github/make-manifests.sh "public/fw/$VER" "$VERSION"
else
  bash .github/make-manifests.sh public/fw "$VERSION"
fi
bash .github/archive.sh public/fw "$VER" "$VERSION" "$SITE" .github/archive-seed.txt "$BETA"
# the stable is the list's first, as archive.sh wrote it: a beta's run did not build it
STABLE=$(grep -v '"beta":' public/fw/versions.json | sed -n 's/.*"version": "\([^"]*\)", "text": "\([^"]*\)".*/\1|\2/p' | head -n 1)
[ -n "$STABLE" ] || { echo "fw/versions.json has no stable version" >&2; exit 1; }
BETA_VER=""
if [ "$BETA" = 1 ]; then
  BETA_VER="$VER"
fi
printf '{"version":"%s","beta":"%s","stats":"%s"}\n' "${STABLE%%|*}" "$BETA_VER" "$STATS" > public/version.json
sed -i "s|@VERSION@|${STABLE#*|}|g" public/index.html public/previous/index.html
find public -type f | sort
```

Then `upload-artifact` of `public/fw` as `site-fw` (90 days), `upload-pages-artifact@v3`, `deploy-pages@v4`.

The version text is `VER, build N (sha)`: `0.15, build 46 (3ddd82a)`, or `0.16.2 beta, build 51 (afbd574)` for a
beta. VER is what people and boards compare, build and sha say which push. `@VERSION@` in the pages is replaced
by `sed` with the stable's text; the page detects a local copy by the `@` still being there and asks
`version.json` instead.

`stats` in `version.json` is a switch: `server/public-url.txt` names the server where boards are counted for the
site's map of boards, and empty means nowhere. The page and the boards both read it here, so the counting starts
and stops with that one file. A board takes only an `https://` address with nothing odd in it, and an answer cut
short gives none.

## make-manifests.sh

Whole file in `../files/flight-radar/make-manifests.sh`. The heart:

```bash
parts=""
while read -r addr path; do
  case "$addr" in ""|--*) continue ;; esac          # the first line is the flash settings
  file=$(basename "$path")
  [ -f "$dir/$file" ] || { echo "$disp: $file is missing" >&2; exit 1; }
  parts="$parts${parts:+, }{ \"path\": \"$file\", \"offset\": $((addr)) }"
done < "$dir/flash_args"
```

- `flash_args` is the only source of offsets. `$((addr))` turns 0x8000 into 32768.
- `basename`: in the build folder the parts sit in subfolders, on the site all side by side.
- The `case` at the top names each board and its `chipFamily`, "experimental" in the name of a board not run on
  one yet. An unknown folder is skipped, so a new board must get a line here or it has no manifest and no place
  in `versions.json`.
- `${name//\"/\\\"}` escapes the inch mark of `2.8"` for JSON.
- It takes any folder of board folders: `archive.sh` runs it on each older version's folder and on the stable a
  beta's run keeps.

Offsets by chip: `local-flash.md`.

## Versions: one line feeds everything

The version is the first `## ` line of `CHANGES.md` at the repo root:

```markdown
# Versions

What changed in each version, the newest first. The first `## ` line below is the version: the firmware
is built with it, the site publishes it, and a board compares its own with it.
The site keeps the firmware of each version: a new number here keeps the one before it as it was.
A version marked (beta) is offered on the site beside the stable one, which stays what the site flashes
unless the beta is picked. Only a board that runs a beta hears of a newer beta.

## 0.16.2 (beta)

- The planes move smoothly. ...
```

It goes to six places, and each one reads the same file, so they cannot disagree:

1. **firmware/CMakeLists.txt** into compile definitions:

```cmake
set(CHANGES "${CMAKE_CURRENT_LIST_DIR}/../CHANGES.md")
file(STRINGS "${CHANGES}" FLIGHT_VERSION REGEX "^## [0-9]" LIMIT_COUNT 1)
# "(beta)" on that line: a beta build. the site keeps it beside the stable, and it follows the site's
# betas where a stable build follows only the stable (main.c update_check)
set(FLIGHT_BETA 0)
if(FLIGHT_VERSION MATCHES "\\(beta\\)")
    set(FLIGHT_BETA 1)
endif()
string(REGEX REPLACE "^## ([0-9.]+).*" "\\1" FLIGHT_VERSION "${FLIGHT_VERSION}")
if(NOT FLIGHT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "no version in ${CHANGES}: its first \"## \" line should be one, like ## 0.3")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHANGES}")   # a new version: configure again
idf_build_set_property(COMPILE_DEFINITIONS "FLIGHT_VERSION=\"${FLIGHT_VERSION}\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "FLIGHT_BETA=${FLIGHT_BETA}" APPEND)
```

   `CMAKE_CONFIGURE_DEPENDS` is what makes a new number reach an old build folder. `file(STRINGS)` sits after
   `include(project.cmake)` and before `project()`.

2. **The PC preview** (`tools/preview/BUILD.BAT`) with findstr, and **the web twin** (`web/BUILD.BAT`, the web
   job's `sed`):

```bat
for /f "tokens=2" %%v in ('findstr /b /c:"## " ..\..\CHANGES.md') do if not defined VER set VER=%%v
```

3. **public/version.json** `{"version":"0.15","beta":"0.16.2","stats":"..."}`, what the boards ask for. No build
   number: a rebuild of 0.15 must not tell every board there is news. `version` is the stable, `beta` the beta
   (`beta-and-stable.md`).
4. **The page**: `CHANGES.md` is copied next to it and rendered as the VERSIONS section, text only, nothing from
   the file goes in as HTML. The page also compares the board's version with the one it follows: "Firmware 0.3,
   this page has 0.4: flash it, no to erase keeps these settings".
5. **The manifests**, as the text with the build number.
6. **fw/versions.json**, the archive's list (`archive.md`).

A new version is a new `## ` section at the top. Nothing else to edit. The number also decides the archive: the
same number replaces, a new number keeps the old.

### Numbers compare number by number

`0.9 < 0.10`, `0.16.1 < 0.16.2`. Four implementations, one rule:

```c
// core/ui_about.c
static long part(const char **s)
{
    long v = 0;
    while (**s >= '0' && **s <= '9') v = v * 10 + (*(*s)++ - '0');
    if (**s == '.') (*s)++;
    return v;
}
int ui_version_cmp(const char *a, const char *b)
{
    for (int k = 0; k < 8 && (*a || *b); k++) {        // no version has eight numbers: a stray letter ends it
        long x = part(&a), y = part(&b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}
```

```js
function vcmp(a, b) {                                // 0.9 < 0.10: number by number (the old page)
  const x = String(a).split('.').map(n => parseInt(n, 10) || 0), y = String(b).split('.').map(n => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    const d = (x[i] || 0) - (y[i] || 0);
    if (d) {
      return d;
    }
  }
  return 0;
}
const older = (a, b) => a.localeCompare(b, 'en', { numeric: true }) < 0;   // the new page
```

In shell, `sort -V`.

### The board asks

`firmware/main/main.c`, in the fetch task once the WiFi is up, then after each round of real work:

```c
#define UPDATE_URL      SITE_URL "version.json"
#define UPDATE_EVERY_MS (12u * 3600u * 1000u)
#define UPDATE_RETRY_MS (30u * 60u * 1000u)   /* no answer, or not one: again in half an hour */

static void update_check(void)
{
    static uint32_t asked;                             // ms of the last question, 0: never
    static int heard;
    uint32_t now = now_ms();
    if (!net_up() || (asked && now - asked < (heard ? UPDATE_EVERY_MS : UPDATE_RETRY_MS))) return;
    asked = now ? now : 1;
    char buf[160], v[sizeof s_newest] = "";
    text_t t = { buf, 0, sizeof buf };
    buf[0] = 0;
    int status = 0;
    net_get(UPDATE_URL, NULL, to_text, &t, &status);
    heard = status == 200 && json_str(buf, "version", v, sizeof v) && version_like(v);
#if FLIGHT_BETA
    /* a beta follows the betas: the site's beta while it has one above its stable, then the stable,
     * which is what a beta found good becomes. a stable build never hears of a beta */
    char b[sizeof s_newest] = "";
    if (heard && json_str(buf, "beta", b, sizeof b) && version_like(b) && ui_version_cmp(b, v) > 0) {
        snprintf(v, sizeof v, "%s", b);
    }
#endif
    ...   /* the stats address, then s_newest under the lock */
}
```

Measured on the board: `HTTP 200, 0.3`. `json_str` is the console's flat reader (`usb-console.md`): no JSON
library. Only digits and dots are believed (`version_like`): this string goes to the screen.

**NEW VERSION on the board** (measured): when `ui_version_cmp(newest, FLIGHT_VERSION) > 0` a small note shows
`NEW VERSION 0.4`: in the panel's title over the radar (NEW and the number on the 2.8"), under the town's name
over the map and over the radar without its panel, under home on the round glass. A tap opens ABOUT. Nothing
else changes. There is no auto-update: the partition table has no OTA slot, and a person flashes from the site.

**ABOUT** (`core/ui_about.c`): this version, what the site has ("0.4 is on the site", "the newest one", or
"newer than the site's 0.3" for a local build or a beta), the board, whether a key is stored, the site's address,
a QR code to the site. The micro pixel font has no `"` and no `&`: use other words.

**The QR code** is baked, not computed on the board: `tools/QrBake` (.NET, own encoder, version 4, level M, 33
modules) writes `core/qr_site.h` and `.c` with `SITE_URL` and a 33 x 5 byte table. Drawn black on a light plate
with a 2-module quiet edge, 4, 5 or 3 pixels a module on the 2.8", 3.5" and round screens. Checked by reading the
code off the preview shots with ZXing (measured). The same `SITE_URL` serves `version.json`, so a fork changes one
thing: run QrBake with its own address.

Testing the newer path without a release: a temporary build with `UPDATE_URL` at a local site whose
`version.json` says a higher number. Reverted after.

## Crash backtraces from a site-flashed board

A panic prints addresses. A person pastes them. Take the ELF of the SAME run:

```
gh run download <run id> -n elf-st7796 -D elf      # or the run's page, Artifacts, signed in
xtensa-esp32-elf-addr2line -pfiaC -e elf/.../owlos-flight-radar.elf 0x400d1234 0x400d5678
```

`riscv32-esp-elf-addr2line` on the C3, C5 and P4. A core dump over UART (CORE DUMP START .. END), measured on
the C3:

```
python -m esp_coredump --chip esp32c3 info_corefile -t b64 -c dump -s core.elf app.elf
riscv32-esp-elf-gdb -batch -ex "add-symbol-file <esp-rom-elfs>/esp32c3_rev3_rom.elf" -ex "thread apply all bt" app.elf core.elf
```

The JC8012 and the Waveshare 7 and 7B keep the dump on flash instead, to read later with `idf.py coredump-info`
(`local-flash.md`, the partition tables). Firmware stays on the site for good, the ELFs go after 90 days. To keep
them, add them to the archive.

## Release rehearsal (measured, 0.3, and again for 0.16.1)

Before a push, make what the push would make:

1. Export the would-be commit with a temporary `GIT_INDEX_FILE` and `git -c core.autocrlf=false archive` into an
   empty folder. No working-tree surprises, no CRLF.
2. Build each board there with CI's own `idf.py --no-ccache -B build_<board> -DSDKCONFIG=... build`, with a new
   sdkconfig (the Flight Radar keeps these as `firmware/build_rel_<board>`). For 0.16.1 the three bench builds came
   out byte for byte the size of the day-to-day builds. Docker was not running, so the build PC's IDF 5.5 stood
   in for `release-v5.5`.
3. Assemble `public/` with the workflow's lines (`tools/assemble.sh`, `site.md`), a made-up build number (an
   export has no git). For the older versions, a local mirror of the live `fw/versions.json` with small stub
   files served on a local port, so the 170 MB of the real archive are not fetched. The stub `flash_args` must be
   real-shaped: `archive.sh` makes the older manifests again from them.
4. Serve it, run `check-site.sh`.
5. Flash parts at manifest offsets with `flash-manifest.ps1`, old version first, set settings, new version
   without erase, compare with `cfgrig.ps1 -SaveCfg / -CompareCfg`.

That is how the 0.2 to 0.3 update was proved before anyone got it. A whole-flash `-Backup` first, restored
after.

## The job logs

**Job logs need auth.** With no token, `GET /repos/.../actions/jobs/<id>/logs` answers 403 "Must have admin rights
to Repository". The public API gives jobs and steps with their conclusions only:

```
curl -s https://api.github.com/repos/<you>/<repo>/actions/runs?per_page=5
curl -s https://api.github.com/repos/<you>/<repo>/actions/runs/<id>/jobs
```

So with no `gh` login on the PC, an agent sees WHICH step failed and must ask a person for its text, or read the
docs. Artifacts of a run, with sizes and expiry, are public too: `.../actions/runs/<id>/artifacts`.

## Pages answers 503 now and then

A run asks the live site for hundreds of files (`archive.sh`), and Pages answers 503 now and then (the
workflow's own note; how often is not recorded). One such answer would stop the run. So every fetch from the
site retries a passing error, and an answer that stays bad still stops the run:

```bash
RETRY="--retry 4 --retry-delay 5"     # curl retries 408, 429, 500, 502, 503, 504 and timeouts
code=$(curl -sS $RETRY -o "$out" -w '%{http_code}' "$URL/$path?run=${GITHUB_RUN_ID:-0}")
```
